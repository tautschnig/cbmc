/// Python to GOTO converter — Call (PLR §6.3.4)
/// implementation. The single largest cohesive block of
/// the converter: dispatches builtins, @c_intrinsic
/// library functions, class instantiation, method calls,
/// and user-defined-function calls.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/string_constant.h>
#include <util/string_expr.h>
#include <util/symbol.h>

#include <solvers/strings/python_regex_to_smt.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <tuple>

// Parse a decimal.Decimal literal string into its base-10 parts
// (sign, coefficient, exponent, is_special, special_kind) for the exact
// Decimal model (see doc/python-frontend-decimal-plan.md). Returns
// nullopt for shapes we don't model exactly (malformed, or > 18
// coefficient digits which would overflow the 64-bit coefficient), so
// the caller falls through to conservative construction.
//   "10.5" -> (0,105,-1,0,0)   "1.00" -> (0,100,-2,0,0)
//   "3" -> (0,3,0,0,0)  "-0" -> (1,0,0,0,0)  "1.5e3" -> (0,15,2,0,0)
//   "inf"/"-inf"/"nan"/"snan" -> is_special set
static std::optional<std::tuple<long, long long, long, long, long>>
parse_decimal_literal(const std::string &in)
{
  std::string s = in;
  long sign = 0;
  std::size_t i = 0;
  if(i < s.size() && (s[i] == '+' || s[i] == '-'))
  {
    sign = (s[i] == '-') ? 1 : 0;
    ++i;
  }
  std::string rest = s.substr(i);
  std::string low;
  for(char c : rest)
    low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if(low == "inf" || low == "infinity")
    return std::make_tuple(sign, 0LL, 0L, 1L, sign ? 2L : 1L);
  if(low == "nan")
    return std::make_tuple(sign, 0LL, 0L, 1L, 3L);
  if(low == "snan")
    return std::make_tuple(sign, 0LL, 0L, 1L, 4L);

  // Numeric: mantissa [eE [+-] exp].
  std::string mant = rest;
  long exp_explicit = 0;
  std::size_t epos = rest.find_first_of("eE");
  if(epos != std::string::npos)
  {
    mant = rest.substr(0, epos);
    const std::string es = rest.substr(epos + 1);
    if(es.empty())
      return std::nullopt;
    errno = 0;
    char *end = nullptr;
    exp_explicit = std::strtol(es.c_str(), &end, 10);
    if(errno != 0 || end == nullptr || *end != '\0')
      return std::nullopt;
  }
  std::size_t dot = mant.find('.');
  std::string digits;
  long fracdigits = 0;
  if(dot == std::string::npos)
    digits = mant;
  else
  {
    digits = mant.substr(0, dot) + mant.substr(dot + 1);
    fracdigits = static_cast<long>(mant.size() - dot - 1);
  }
  if(digits.empty() || digits.size() > 18)
    return std::nullopt;
  for(char c : digits)
    if(!std::isdigit(static_cast<unsigned char>(c)))
      return std::nullopt;
  const long long coeff = std::stoll(digits);
  const long exp = -fracdigits + exp_explicit;
  return std::make_tuple(sign, coeff, exp, 0L, 0L);
}

// PLR §6.3.4: Calls
// "A call calls a callable object (e.g., a function) with a possibly
// empty series of arguments."
exprt python_convertert::convert_call(const jsont &expr)
{
  const jsont &func = json_member(expr, "func");
  const jsont &args = json_member(expr, "args");

  // PLR §6.10: 'TypeError: 'NoneType' object is not callable'.
  // Calling None as a function raises TypeError. Detect both
  // the literal-None callee (Constant(value=None)) and the
  // Name-bound-to-None case (Name resolving to a symbol whose
  // stored value is python_value{NONE}).
  bool callee_is_none = false;
  if(is_node_type(func, "Constant"))
  {
    const jsont &v = json_member(func, "value");
    if(v.is_null())
      callee_is_none = true;
  }
  else if(is_node_type(func, "Name"))
  {
    std::string nm = json_string(json_member(func, "id"));
    if(nm == "None")
      callee_is_none = true;
    else
    {
      const symbolt *s = symbol_table.lookup(irep_idt{"python::" + nm});
      if(s != nullptr && is_python_none_constant(s->value))
        callee_is_none = true;
    }
  }
  if(callee_is_none)
  {
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    const symbolt *exc_type_sym =
      symbol_table.lookup("python::__exception_type");
    if(exc_sym != nullptr)
    {
      pending_checks.push_back(
        code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
      if(exc_type_sym != nullptr)
      {
        long h = exception_type_hash("TypeError");
        pending_checks.push_back(code_frontend_assignt{
          exc_type_sym->symbol_expr(), from_integer(h, exc_type_sym->type)});
      }
    }
    return side_effect_expr_nondett{python_value_type(), get_location(expr)};
  }

  // Stage 1 of the re-precision plan: detect user-visible
  // regex call patterns whose pattern is a constant non-ε
  // accepting regex AND whose subject is statically the empty
  // string. Emit a 'regex-no-match' property at the call site
  // independently of the subsequent dispatch.
  //
  // Patterns recognised:
  //   re.search(p, s) / re.match / re.fullmatch
  //   compile(p).search(s) / .match / .fullmatch
  //   re.compile(p).search(s) / .match / .fullmatch
  // where p is a constant string and s is a constant string,
  // a Name with a recorded string constant, or a one-level
  // dict subscript dict_lit['K'] with a recorded string value.
  {
    auto literal_string_from_ast =
      [&](const jsont &n) -> std::optional<std::string>
    {
      if(is_node_type(n, "Constant"))
      {
        const jsont &v = json_member(n, "value");
        if(v.is_string())
          return v.value;
      }
      if(is_node_type(n, "Name"))
      {
        std::string nm = json_string(json_member(n, "id"));
        irep_idt sid{qualify_name(nm)};
        auto si = string_constants.find(sid);
        if(si != string_constants.end())
          return si->second;
      }
      if(is_node_type(n, "Subscript"))
      {
        const jsont &v_node = json_member(n, "value");
        const jsont &s_node = json_member(n, "slice");
        if(!is_node_type(s_node, "Constant"))
          return std::nullopt;
        const jsont &k = json_member(s_node, "value");
        if(!k.is_string())
          return std::nullopt;
        if(is_node_type(v_node, "Name"))
        {
          std::string nm = json_string(json_member(v_node, "id"));
          irep_idt did{qualify_name(nm)};
          auto si = dict_literal_value_string_consts.find(did);
          if(si == dict_literal_value_string_consts.end())
            return std::nullopt;
          auto ki = si->second.find(k.value);
          if(ki == si->second.end())
            return std::nullopt;
          return ki->second;
        }
      }
      return std::nullopt;
    };

    auto detect_regex_method = [&](const jsont &n) -> const char *
    {
      if(!is_node_type(n, "Attribute"))
        return nullptr;
      std::string at = json_string(json_member(n, "attr"));
      if(at == "search" || at == "match" || at == "fullmatch")
        return "search-like";
      return nullptr;
    };

    std::optional<std::string> pat_lit;
    std::optional<std::string> subj_lit;
    bool is_regex_call = false;

    if(is_node_type(func, "Attribute") && detect_regex_method(func))
    {
      const jsont &recv = json_member(func, "value");
      // Form A: re.<method>(p, s).
      if(
        is_node_type(recv, "Name") &&
        json_string(json_member(recv, "id")) == "re" && args.is_array() &&
        as_array(args).size() >= 2)
      {
        auto ai = as_array(args).begin();
        pat_lit = literal_string_from_ast(*ai);
        ++ai;
        subj_lit = literal_string_from_ast(*ai);
        is_regex_call = true;
      }
      // Form B: compile(p).<method>(s) or re.compile(p).<method>(s).
      else if(is_node_type(recv, "Call"))
      {
        const jsont &cf = json_member(recv, "func");
        bool is_compile = false;
        if(
          is_node_type(cf, "Name") &&
          json_string(json_member(cf, "id")) == "compile")
          is_compile = true;
        else if(
          is_node_type(cf, "Attribute") &&
          json_string(json_member(cf, "attr")) == "compile")
        {
          const jsont &cv = json_member(cf, "value");
          if(
            is_node_type(cv, "Name") &&
            json_string(json_member(cv, "id")) == "re")
            is_compile = true;
        }
        if(is_compile)
        {
          const jsont &cargs = json_member(recv, "args");
          if(cargs.is_array() && !as_array(cargs).empty())
            pat_lit = literal_string_from_ast(*as_array(cargs).begin());
          if(args.is_array() && !as_array(args).empty())
            subj_lit = literal_string_from_ast(*as_array(args).begin());
          is_regex_call = true;
        }
      }
    }

    if(
      is_regex_call && pat_lit.has_value() && subj_lit.has_value() &&
      subj_lit->empty())
    {
      auto can_match_empty = python_regex_can_match_empty(*pat_lit);
      if(can_match_empty.has_value() && !*can_match_empty)
      {
        source_locationt rloc = get_location(expr);
        rloc.set_property_class("regex-no-match");
        rloc.set_comment("regex '" + *pat_lit + "' cannot match empty string");
        code_assertt rne{false_exprt{}};
        rne.add_source_location() = rloc;
        code_blockt rne_block;
        rne_block.add(std::move(rne));
        pending_checks.push_back(std::move(rne_block));
      }
    }
  }

  // §12 container-stored callables: dispatch a call whose callee is a
  // subscript of a dict *literal* whose values are all callables, e.g.
  // `{'+': lambda: 1.0, '-': minus}[op](args)`. Select the entry whose
  // key equals the subscript and call it; a non-matching key is a
  // KeyError (PLR §6.10). Falls through to the generic (nondet) callee
  // path when the values are not all resolvable callables.
  if(is_node_type(func, "Subscript"))
  {
    const jsont &container = json_member(func, "value");
    if(is_node_type(container, "Dict"))
    {
      const jsont &keys = json_member(container, "keys");
      const jsont &vals = json_member(container, "values");
      if(
        keys.is_array() && vals.is_array() && !as_array(keys).empty() &&
        as_array(keys).size() == as_array(vals).size())
      {
        std::vector<irep_idt> callables;
        bool all_callable = true;
        for(const auto &v : as_array(vals))
        {
          irep_idt cid;
          if(is_node_type(v, "Lambda"))
          {
            exprt l = convert_lambda(v);
            if(l.id() == ID_symbol && l.type().id() == ID_code)
              cid = to_symbol_expr(l).get_identifier();
          }
          else if(is_node_type(v, "Name"))
          {
            std::string nm = json_string(json_member(v, "id"));
            auto ai = function_aliases.find(qualify_name(nm));
            if(ai != function_aliases.end())
              cid = ai->second;
            else
            {
              const symbolt *bs =
                symbol_table.lookup(irep_idt{"python::" + nm});
              if(bs != nullptr && bs->type.id() == ID_code)
                cid = bs->name;
            }
          }
          if(cid.empty())
          {
            all_callable = false;
            break;
          }
          callables.push_back(cid);
        }

        exprt::operandst call_args;
        bool args_ok = all_callable;
        if(all_callable && args.is_array())
          for(const auto &a : as_array(args))
          {
            exprt ae = convert_expression(a);
            if(ae.is_nil())
            {
              args_ok = false;
              break;
            }
            call_args.push_back(ae);
          }

        exprt key_expr = args_ok
                           ? convert_expression(json_member(func, "slice"))
                           : nil_exprt{};

        if(args_ok && key_expr.is_not_nil())
        {
          const symbolt &c0 = symbol_table.lookup_ref(callables[0]);
          typet ret_t = to_code_type(c0.type).return_type();
          if(ret_t.id() == ID_empty)
            ret_t = python_value_type();
          static unsigned disp_ctr = 0;
          std::string rn = "__call_dispatch_" + std::to_string(disp_ctr++);
          irep_idt rid{qualify_name(rn)};
          if(symbol_table.lookup(rid) == nullptr)
          {
            symbolt rs{rid, ret_t, "python"};
            rs.base_name = rn;
            rs.is_lvalue = true;
            rs.is_state_var = true;
            rs.is_static_lifetime = current_function.empty();
            symbol_table.add(rs);
          }
          symbol_exprt result = symbol_table.lookup_ref(rid).symbol_expr();
          // Build the dispatch statements locally; commit to
          // pending_checks only if every key type-checks, so a partial
          // build never leaks spurious (side-effecting) calls.
          std::vector<codet> disp;
          // Start nondet (covers the no-key-matched fallback).
          disp.push_back(code_frontend_assignt{
            result, side_effect_expr_nondett{ret_t, get_location(expr)}});

          exprt any_match = false_exprt{};
          auto kit = as_array(keys).begin();
          bool key_types_ok = true;
          for(std::size_t i = 0; i < callables.size(); ++i, ++kit)
          {
            exprt kexpr = convert_expression(*kit);
            if(kexpr.is_nil() || kexpr.type() != key_expr.type())
            {
              key_types_ok = false;
              break;
            }
            equal_exprt eq{key_expr, kexpr};
            const symbolt &cs = symbol_table.lookup_ref(callables[i]);
            side_effect_expr_function_callt call{
              cs.symbol_expr(),
              call_args,
              to_code_type(cs.type).return_type(),
              get_location(expr)};
            exprt cv = call;
            if(cv.type() != ret_t)
              cv = typecast_exprt{std::move(cv), ret_t};
            code_blockt body;
            body.add(code_frontend_assignt{result, std::move(cv)});
            disp.push_back(code_ifthenelset{eq, std::move(body)});
            any_match = (any_match.id() == ID_false)
                          ? static_cast<exprt>(eq)
                          : static_cast<exprt>(or_exprt{any_match, eq});
          }
          if(key_types_ok)
          {
            for(auto &c : disp)
              pending_checks.push_back(std::move(c));
            // PLR §6.10: a subscript key that matches no entry is a KeyError.
            emit_conditional_exception(not_exprt{any_match}, "KeyError");
            return std::move(result);
          }
        }
      }
    }
  }

  std::string func_name;
  if(is_node_type(func, "Name"))
  {
    func_name = json_string(json_member(func, "id"));
    // PLR §9.3: `cls(...)` inside a classmethod constructs the
    // enclosing class. cls is bound as a pointer-to-class parameter;
    // rewrite to the class name so the constructor dispatch below
    // builds a real instance instead of leaving the result nondet.
    if(
      func_name == "cls" && !current_class.empty() &&
      class_types.count(current_class))
    {
      const symbolt *cp =
        symbol_table.lookup(irep_idt{"python::" + current_function + "::cls"});
      if(cp != nullptr && cp->type.id() == ID_pointer)
        func_name = current_class;
    }
  }
  else if(is_node_type(func, "Attribute"))
  {
    // Method-call dispatch (obj.method(args)). Extracted to
    // python_converter_call_method.cpp for clarity. May rebind
    // func_name to the method name for downstream user-call
    // resolution (e.g. math.ceil() called on non-module receiver).
    if(auto r = try_method_call(expr, func_name, args))
      return std::move(*r);
  }
  else if(is_node_type(func, "Lambda"))
  {
    // PLR §6.14: immediately-applied lambda `(lambda ...: ...)(args)`.
    // Convert the lambda to its function symbol and dispatch the call to
    // it by name, exactly as `g = lambda ...; g(args)` does.
    exprt l = convert_lambda(func);
    if(l.id() == ID_symbol && l.type().id() == ID_code)
      func_name = id2string(to_symbol_expr(l).get_identifier()).substr(8);
  }
  // Verification-primitive dispatch group (nondet_*, randint,
  // assume family, __cbmc_re_* regex hooks). Extracted to
  // python_converter_call_nondet.cpp for clarity.
  if(auto r = try_nondet_call(expr, func_name, args))
    return std::move(*r);
  // PLR §22.7.1: typing.NewType-defined callable aliases.
  // 'X = NewType(...); X(arg)' is the identity: X(arg) == arg.
  if(
    newtype_aliases.count(func_name) > 0 && args.is_array() &&
    !as_array(args).empty())
  {
    return convert_expression(*as_array(args).begin());
  }
  // PLR §math: 'from math import X' direct calls. When X
  // is a math-int intrinsic (factorial / comb / perm / gcd /
  // lcm / isqrt) and all arguments are integer constants,
  // emit the precise constant-fold instead of dispatching
  // to the library placeholder (which returns 0). Mirrors
  // the fold inside try_method_call's 'math.X' dispatch.
  if(!func_name.empty() && math_imports.count(func_name) > 0 && args.is_array())
  {
    const std::string &mn = math_imports[func_name];
    if(
      mn == "factorial" || mn == "comb" || mn == "perm" || mn == "gcd" ||
      mn == "lcm" || mn == "isqrt")
    {
      auto get_int_args = [&]() -> std::optional<std::vector<long long>>
      {
        std::vector<long long> out;
        for(const auto &a : as_array(args))
        {
          exprt e = convert_expression(a);
          bool negate = false;
          if(e.id() == ID_unary_minus && e.operands().size() == 1)
          {
            e = e.operands()[0];
            negate = true;
          }
          if(
            e.id() == ID_typecast && e.operands().size() == 1 &&
            e.operands()[0].is_constant() &&
            e.operands()[0].type().id() == ID_signedbv)
            e = e.operands()[0];
          if(!e.is_constant() || e.type().id() != ID_signedbv)
            return std::nullopt;
          mp_integer iv;
          if(to_integer(to_constant_expr(e), iv))
            return std::nullopt;
          long long val = iv.to_long();
          if(negate)
            val = -val;
          out.push_back(val);
        }
        return out;
      };
      auto iargs = get_int_args();
      if(iargs.has_value())
      {
        const auto &v = iargs.value();
        auto make_int = [this](long long x)
        { return from_integer(x, python_int_type()); };
        if(mn == "factorial" && v.size() == 1 && v[0] >= 0)
        {
          long long r = 1;
          for(long long i = 2; i <= v[0]; ++i)
            r *= i;
          return make_int(r);
        }
        if(mn == "isqrt" && v.size() == 1 && v[0] >= 0)
        {
          long long n = v[0];
          long long r = 0;
          while((r + 1) * (r + 1) <= n)
            ++r;
          return make_int(r);
        }
        if(mn == "comb" && v.size() == 2 && v[0] >= 0 && v[1] >= 0)
        {
          long long n = v[0], k = v[1];
          if(k > n)
            return make_int(0);
          if(k > n - k)
            k = n - k;
          long long r = 1;
          for(long long i = 0; i < k; ++i)
          {
            r *= (n - i);
            r /= (i + 1);
          }
          return make_int(r);
        }
        if(mn == "perm" && v.size() >= 1 && v[0] >= 0)
        {
          long long n = v[0];
          long long k = v.size() == 1 ? n : v[1];
          if(k < 0)
            k = 0;
          if(k > n)
            return make_int(0);
          long long r = 1;
          for(long long i = 0; i < k; ++i)
            r *= (n - i);
          return make_int(r);
        }
        if(mn == "gcd")
        {
          long long g = 0;
          for(auto x : v)
          {
            long long a = std::llabs(x);
            while(a)
            {
              long long t = g % a;
              g = a;
              a = t;
            }
          }
          return make_int(g);
        }
        if(mn == "lcm" && !v.empty())
        {
          long long l = std::llabs(v[0]);
          for(std::size_t i = 1; i < v.size(); ++i)
          {
            long long a = std::llabs(v[i]);
            if(l == 0 || a == 0)
            {
              l = 0;
              break;
            }
            long long g = l, t = a;
            while(t)
            {
              long long r = g % t;
              g = t;
              t = r;
            }
            l = (l / g) * a;
          }
          return make_int(l);
        }
      }
    }
  }
  // PLR §4: name resolution. If the user defined a function
  // (or class) with the same name as a Python builtin, the
  // user binding shadows the builtin within the module. Try
  // the user-call dispatch first when a same-named function
  // symbol exists, before falling through to the builtin
  // intercept. Without this, calls like 'sum(2, 2)' (user-
  // defined two-arg sum) match the builtin sum(iterable)
  // intercept and return nondet because the args don't fit.
  if(!func_name.empty())
  {
    irep_idt user_id{"python::" + func_name};
    const symbolt *us = symbol_table.lookup(user_id);
    if(us != nullptr && us->type.id() == ID_code)
    {
      // Defer to the user-call dispatch path below.
    }
    else
    {
      // No user function with this name — try builtins.
      if(auto r = try_builtin_call(expr, func_name, args))
        return std::move(*r);
    }
  }
  else
  {
    if(auto r = try_builtin_call(expr, func_name, args))
      return std::move(*r);
  }

  // Regular function call — check if it's a class constructor
  if(class_types.count(func_name))
  {
    // Sound exact-Decimal model: Decimal(<str/int literal>) is folded at
    // convert time into the base-10 (sign, coeff, exp) struct, because
    // the stub cannot parse a runtime string (float(<runtime str>) is
    // nondet). Non-literal args fall through to the stub __init__.
    // See doc/python-frontend-decimal-plan.md.
    if(func_name == "Decimal" && args.is_array() && as_array(args).size() == 1)
    {
      const jsont &arg0 = *as_array(args).begin();
      std::optional<std::tuple<long, long long, long, long, long>> parts;
      if(is_node_type(arg0, "Constant"))
      {
        const jsont &cv = json_member(arg0, "value");
        if(cv.is_string())
          parts = parse_decimal_literal(cv.value);
        else if(cv.is_number())
        {
          // Integer literal (a float literal falls through — residual).
          const std::string &vs = cv.value;
          if(
            vs.find('.') == std::string::npos &&
            vs.find('e') == std::string::npos &&
            vs.find('E') == std::string::npos)
          {
            errno = 0;
            char *end = nullptr;
            long long iv = std::strtoll(vs.c_str(), &end, 10);
            if(errno == 0 && end != nullptr && *end == '\0')
              parts = std::make_tuple(
                iv < 0 ? 1L : 0L, iv < 0 ? -iv : iv, 0L, 0L, 0L);
          }
        }
      }
      if(parts.has_value())
      {
        const auto [psign, pcoeff, pexp, pspec, pkind] = *parts;
        const struct_typet &dt = to_struct_type(class_types.at("Decimal"));
        exprt::operandst fields;
        for(const auto &comp : dt.components())
        {
          const std::string nm = id2string(comp.get_name());
          long long v = 0;
          bool set = true;
          if(nm == "__class_tag")
            v =
              class_tag_ids.count("Decimal") ? class_tag_ids.at("Decimal") : 0;
          else if(nm == "_sign")
            v = psign;
          else if(nm == "_int")
            v = pcoeff;
          else if(nm == "_exp")
            v = pexp;
          else if(nm == "_is_special")
            v = pspec;
          else if(nm == "_special_kind")
            v = pkind;
          else if(nm.rfind("__shadow_", 0) == 0)
            v = 1; // a directly-built field is present on the instance
          else
            set = false;
          if(set)
            fields.push_back(from_integer(v, comp.type()));
          else
            fields.push_back(safe_zero(comp.type()));
        }
        return struct_exprt{std::move(fields), class_types.at("Decimal")};
      }
    }

    // Constructor call as expression: create temp, call __init__, return temp
    const struct_typet &cls_type = class_types[func_name];
    static unsigned ctor_tmp_counter = 0;
    std::string tmp_name =
      "__ctor_expr_" + func_name + "_" + std::to_string(ctor_tmp_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};

    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, cls_type, "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }

    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);

    // Copy class-level default values from the class object
    irep_idt class_obj_id{"python::" + func_name};
    const symbolt *class_obj = symbol_table.lookup(class_obj_id);
    if(class_obj != nullptr && !class_obj->value.is_nil())
    {
      pending_checks.push_back(
        code_frontend_assignt{tmp_sym.symbol_expr(), class_obj->symbol_expr()});
    }

    auto init_call = build_class_init_call(
      func_name, tmp_sym.symbol_expr(), expr, get_location(expr));
    if(init_call)
    {
      // Inject the __init__ call before the current statement
      pending_checks.push_back(code_expressiont{*init_call});
    }

    return tmp_sym.symbol_expr();
  }
  // User-function-call fallback. Extracted to
  // python_converter_call_user.cpp for clarity.
  return convert_user_call(expr, func_name, args);
}
