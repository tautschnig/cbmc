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
#include <sstream>

// PLR §6.3.4: Calls
// "A call calls a callable object (e.g., a function) with a possibly
// empty series of arguments."
exprt python_convertert::convert_call(const jsont &expr)
{
  const jsont &func = json_member(expr, "func");
  const jsont &args = json_member(expr, "args");

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

  std::string func_name;
  if(is_node_type(func, "Name"))
    func_name = json_string(json_member(func, "id"));
  else if(is_node_type(func, "Attribute"))
  {
    // Method-call dispatch (obj.method(args)). Extracted to
    // python_converter_call_method.cpp for clarity. May rebind
    // func_name to the method name for downstream user-call
    // resolution (e.g. math.ceil() called on non-module receiver).
    if(auto r = try_method_call(expr, func_name, args))
      return std::move(*r);
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
