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
#include <util/pointer_offset_size.h>
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

// ---- Fat-closure runtime (doc/python-frontend-fat-closure-plan.md) ----

std::size_t python_convertert::register_closure(const irep_idt &lambda_id)
{
  for(std::size_t i = 0; i < closure_registry.size(); ++i)
    if(closure_registry[i] == lambda_id)
      return i;
  closure_registry.push_back(lambda_id);
  return closure_registry.size() - 1;
}

typet python_convertert::closure_capture_slot_type(const typet &t) const
{
  // --python-unbounded-ints: a mathematical-integer capture has no
  // byte width -- the capture record is heap-ALLOCATED (needs a
  // size) and field-sensitivity computes member offsets, so an
  // integer_typet slot crashes both. Captures are stored in the
  // flag's usual 64-bit representation (the int2bv convention used
  // at every other narrowing boundary); normalizing HERE, at
  // registration, keeps every consumer consistent (the record type,
  // the callee's capture parameters, the dispatch's member reads
  // and the call-site snapshots all derive from closure_captures).
  if(t.id() == ID_integer)
    return signedbv_typet{64};
  return t;
}

struct_typet python_convertert::closure_record_type(const irep_idt &lambda_id)
{
  struct_typet::componentst comps;
  auto it = closure_captures.find(id2string(lambda_id));
  if(it != closure_captures.end())
    for(const auto &cap : it->second)
      comps.push_back(
        struct_typet::componentt{std::get<1>(cap), std::get<2>(cap)});
  struct_typet st{comps};
  st.set_tag("closure_rec_" + id2string(lambda_id));
  return st;
}

exprt python_convertert::box_closure(
  const irep_idt &lambda_id,
  const exprt::operandst &capture_values,
  std::vector<codet> &out,
  const source_locationt &loc)
{
  struct_typet rec_type = closure_record_type(lambda_id);
  pointer_typet ptr_type{rec_type, 64};
  static unsigned ctr = 0;
  std::string pn = "__closure_rec_" + std::to_string(ctr++);
  irep_idt pid{qualify_name(pn)};
  if(symbol_table.lookup(pid) == nullptr)
  {
    symbolt ps{pid, ptr_type, "python"};
    ps.base_name = pn;
    ps.is_lvalue = true;
    ps.is_state_var = true;
    ps.is_static_lifetime = current_function.empty();
    symbol_table.add(ps);
  }
  symbol_exprt rec_ptr = symbol_table.lookup_ref(pid).symbol_expr();
  namespacet ns{symbol_table};
  exprt size =
    from_integer(pointer_offset_size(rec_type, ns).value_or(8), size_type());
  side_effect_exprt alloc{ID_allocate, {size, false_exprt{}}, ptr_type, loc};
  out.push_back(code_frontend_assignt{rec_ptr, alloc});
  const auto &comps = rec_type.components();
  for(std::size_t i = 0; i < comps.size() && i < capture_values.size(); ++i)
  {
    member_exprt field{
      dereference_exprt{rec_ptr}, comps[i].get_name(), comps[i].type()};
    exprt val = capture_values[i];
    if(val.type() != comps[i].type())
      val = safe_typecast(val, comps[i].type());
    out.push_back(code_frontend_assignt{field, val});
  }
  int closure_idx = static_cast<int>(register_closure(lambda_id));
  // The closure fn-index must stay PRECISE (it selects the dispatch
  // target). Under --python-unbounded-ints the __int_val slot is an
  // INT-ID HANDLE (the 2026-07-21 migration); the previous
  // allocate_boxed_leaf pointer box predates it and stored a
  // pointer-typed field into the handle-typed slot -- an ill-typed
  // struct literal that crashed symex's assign_from_struct.
  // int_to_handle is exact here: the inttab equality pins the
  // handle's image to the constant index.
  exprt fn_stored =
    unbounded_ints ? int_to_handle(from_integer(closure_idx, integer_typet{}))
                   : exprt{from_integer(closure_idx, signedbv_typet{64})};
  return make_python_closure(fn_stored, rec_ptr);
}

exprt python_convertert::box_bound_method(
  const irep_idt &method_id,
  const exprt &self_expr,
  const source_locationt &loc)
{
  const symbolt *ms = symbol_table.lookup(method_id);
  if(ms == nullptr || ms->type.id() != ID_code)
    return nil_exprt{};
  const auto &mparams = to_code_type(ms->type).parameters();
  if(mparams.empty())
    return nil_exprt{}; // no self slot — not a bound method
  const typet self_t = mparams[0].type();
  // Register `self` as the (single) capture for this method. The capture
  // record will hold self; the method's own first parameter IS self, so
  // dispatch prepends the capture (see bound_method_closures).
  closure_captures[id2string(method_id)] = {
    std::make_tuple(std::string{}, std::string{"__self"}, self_t)};
  bound_method_closures.insert(register_closure(method_id));
  // Coerce the receiver to the method's self-parameter type (mirrors
  // try_method_call): a pointer self takes the address, otherwise a
  // value cast.
  exprt self_val = self_expr;
  if(self_val.type() != self_t)
  {
    if(self_t.id() == ID_pointer && self_val.type().id() != ID_pointer)
      self_val = address_of_exprt{self_val};
    else
      self_val = safe_typecast(self_val, self_t);
  }
  std::vector<codet> out;
  exprt boxed = box_closure(method_id, {self_val}, out, loc);
  for(auto &c : out)
    pending_checks.push_back(std::move(c));
  return boxed;
}

exprt python_convertert::dispatch_closure_value(
  const exprt &closure_val,
  const exprt::operandst &args,
  const source_locationt &loc)
{
  const typet ret_t = python_value_type();
  static unsigned dctr = 0;
  std::string rn = "__closure_call_" + std::to_string(dctr++);
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
  std::vector<codet> disp;
  disp.push_back(
    code_frontend_assignt{result, side_effect_expr_nondett{ret_t, loc}});
  const exprt fn = python_value_closure_fn(closure_val);
  bool any = false;
  for(std::size_t k = 0; k < closure_registry.size(); ++k)
  {
    const irep_idt &lid = closure_registry[k];
    const symbolt *ls = symbol_table.lookup(lid);
    if(ls == nullptr || ls->type.id() != ID_code)
      continue;
    const auto &lparams = to_code_type(ls->type).parameters();
    auto ci = closure_captures.find(id2string(lid));
    std::size_t ncap = (ci != closure_captures.end()) ? ci->second.size() : 0;
    std::size_t nuser = lparams.size() >= ncap ? lparams.size() - ncap : 0;
    if(nuser != args.size())
      continue; // positional arity mismatch -> not this candidate
    any = true;
    const bool self_first = bound_method_closures.count(k) > 0;
    // For an ordinary closure the captures are appended LAST, so user
    // arg i maps to lparams[i]; for a bound method `self` is the FIRST
    // parameter (captures prepended), so user arg i maps to
    // lparams[ncap + i].
    struct_typet rec_type = closure_record_type(lid);
    pointer_typet rec_ptr_t{rec_type, 64};
    exprt rec_ptr =
      typecast_exprt{python_value_closure_rec(closure_val), rec_ptr_t};
    exprt::operandst user_args;
    for(std::size_t i = 0; i < args.size(); ++i)
    {
      exprt a = args[i];
      const std::size_t pidx = self_first ? ncap + i : i;
      if(pidx < lparams.size() && a.type() != lparams[pidx].type())
      {
        if(
          is_python_value_type(lparams[pidx].type()) &&
          !is_python_value_type(a.type()))
          a = wrap_value(a);
        else
          a = safe_typecast(a, lparams[pidx].type());
      }
      user_args.push_back(a);
    }
    exprt::operandst capture_args;
    for(const auto &c : rec_type.components())
      capture_args.push_back(
        member_exprt{dereference_exprt{rec_ptr}, c.get_name(), c.type()});
    exprt::operandst call_args;
    if(self_first)
    {
      // self (captures) first, then user args
      for(auto &c : capture_args)
        call_args.push_back(std::move(c));
      for(auto &u : user_args)
        call_args.push_back(std::move(u));
    }
    else
    {
      for(auto &u : user_args)
        call_args.push_back(std::move(u));
      for(auto &c : capture_args)
        call_args.push_back(std::move(c));
    }
    side_effect_expr_function_callt call{
      ls->symbol_expr(), call_args, to_code_type(ls->type).return_type(), loc};
    exprt cv = call;
    if(cv.type() != ret_t)
      cv = is_python_value_type(cv.type()) ? cv : wrap_value(cv);
    code_blockt body;
    body.add(code_frontend_assignt{result, cv});
    disp.push_back(code_ifthenelset{
      and_exprt{
        python_value_is(closure_val, python_type_tagt::CLOSURE),
        equal_exprt{fn, from_integer(static_cast<int>(k), signedbv_typet{64})}},
      std::move(body)});
  }
  if(!any)
    return nil_exprt{};
  for(auto &c : disp)
    pending_checks.push_back(std::move(c));
  return std::move(result);
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

  // ---- re.escape(x): sound model ----
  // The library stub returns "" (an empty regex), which matches EVERYTHING:
  // e.g. `re.match(re.escape("abc"), "zzz")` wrongly verifies as a match (a
  // false proof; CPython returns None). Model it soundly: for a CONSTANT x,
  // return the properly-escaped literal so the downstream regex matches x
  // literally (precise); for a symbolic x (whose content we cannot escape),
  // return a sound nondet string so the downstream match stays nondet.
  if(
    is_node_type(func, "Attribute") &&
    json_string(json_member(func, "attr")) == "escape")
  {
    const jsont &erecv = json_member(func, "value");
    if(
      is_node_type(erecv, "Name") &&
      json_string(json_member(erecv, "id")) == "re" && args.is_array() &&
      !as_array(args).empty())
    {
      const jsont &xarg = *as_array(args).begin();
      std::optional<std::string> xs;
      if(is_node_type(xarg, "Constant"))
      {
        const jsont &v = json_member(xarg, "value");
        if(v.is_string())
          xs = v.value;
      }
      else if(is_node_type(xarg, "Name"))
      {
        auto si = string_constants.find(
          irep_idt{qualify_name(json_string(json_member(xarg, "id")))});
        if(si != string_constants.end())
          xs = si->second;
      }
      if(xs.has_value())
      {
        // CPython 3.7+ re.escape: backslash-prefix the regex special chars.
        static const std::string special = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f";
        std::string out;
        for(char c : *xs)
        {
          if(special.find(c) != std::string::npos)
            out.push_back('\\');
          out.push_back(c);
        }
        return python_string_literal(out);
      }
      // Symbolic x: cannot escape unknown content -> sound nondet pattern.
      return bounded_nondet_string(get_location(expr));
    }
  }

  // ---- re IGNORECASE/DOTALL flag plumbing (whole-group, all re entry points)
  // A constant `flags=` argument to re.<fn> does not constant-propagate into
  // the re stub body (the stub's `flags` parameter stays symbolic), so the
  // inline-flag prefix the stub would add via `_re_flag_prefix` is lost and the
  // match degrades to nondet. When `flags` is a compile-time constant AND the
  // pattern is a string literal, rewrite the call here: prepend the inline-flag
  // group ((?i)/(?s)/(?is)) to the literal pattern and drop the flags argument,
  // so the stub (and the regex translator/constant matcher, which already
  // honour leading inline-flag groups) receive a flag-free constant pattern.
  // Only the exact translator-supported flag values map to a prefix; any other
  // flag (or a non-constant flag / non-literal pattern) is left unchanged ->
  // sound nondet (never a guessed match). One uniform hook for every re entry.
  if(is_node_type(func, "Attribute"))
  {
    const std::string re_attr = json_string(json_member(func, "attr"));
    // Positional index of `flags` per re function (pattern is always index 0).
    int flag_pos_idx = -1;
    if(
      re_attr == "match" || re_attr == "search" || re_attr == "fullmatch" ||
      re_attr == "findall" || re_attr == "finditer")
      flag_pos_idx = 2;
    else if(re_attr == "compile")
      flag_pos_idx = 1;
    else if(re_attr == "sub" || re_attr == "subn")
      flag_pos_idx = 4;
    const jsont &re_recv = json_member(func, "value");
    const bool recv_is_re = is_node_type(re_recv, "Name") &&
                            json_string(json_member(re_recv, "id")) == "re";
    if(flag_pos_idx >= 0 && recv_is_re && args.is_array())
    {
      // Pure evaluator for re flag expressions (no side effects): constant
      // ints, `re.<FLAG>` attributes, and `|` combinations thereof.
      std::function<std::optional<long>(const jsont &)> eval_re_flags =
        [&](const jsont &n) -> std::optional<long>
      {
        if(is_node_type(n, "Constant"))
        {
          const jsont &v = json_member(n, "value");
          if(v.is_number())
          {
            try
            {
              return std::stol(v.value);
            }
            catch(...)
            {
              return std::nullopt;
            }
          }
          return std::nullopt;
        }
        if(is_node_type(n, "Attribute"))
        {
          const jsont &rv = json_member(n, "value");
          if(
            is_node_type(rv, "Name") &&
            json_string(json_member(rv, "id")) == "re")
          {
            const std::string fa = json_string(json_member(n, "attr"));
            if(fa == "IGNORECASE" || fa == "I")
              return 2;
            if(fa == "LOCALE" || fa == "L")
              return 4;
            if(fa == "MULTILINE" || fa == "M")
              return 8;
            if(fa == "DOTALL" || fa == "S")
              return 16;
            if(fa == "UNICODE" || fa == "U")
              return 32;
            if(fa == "VERBOSE" || fa == "X")
              return 64;
            if(fa == "ASCII" || fa == "A")
              return 256;
          }
          return std::nullopt;
        }
        if(
          is_node_type(n, "BinOp") &&
          is_node_type(json_member(n, "op"), "BitOr"))
        {
          auto l = eval_re_flags(json_member(n, "left"));
          auto r = eval_re_flags(json_member(n, "right"));
          if(l.has_value() && r.has_value())
            return *l | *r;
        }
        return std::nullopt;
      };

      // Locate the flags node: keyword `flags=` or the positional slot.
      const jsont *flags_node = nullptr;
      bool flags_is_kw = false;
      const jsont &re_kws = json_member(expr, "keywords");
      if(re_kws.is_array())
        for(const auto &kw : as_array(re_kws))
          if(json_string(json_member(kw, "arg")) == "flags")
          {
            flags_node = &json_member(kw, "value");
            flags_is_kw = true;
          }
      const json_arrayt &old_args = as_array(args);
      if(
        flags_node == nullptr &&
        static_cast<int>(old_args.size()) > flag_pos_idx)
        flags_node = &*std::next(old_args.begin(), flag_pos_idx);

      if(flags_node != nullptr && !old_args.empty())
      {
        const std::optional<long> fv = eval_re_flags(*flags_node);
        // Map to a supported inline-flag prefix. f == 0 needs no prefix
        // (skip); an unsupported flag/combination leaves the call unchanged
        // (sound nondet).
        std::string pfx;
        bool supported = fv.has_value();
        if(fv.has_value())
        {
          if(*fv == 0)
            ; // no prefix needed
          else if(*fv == 2)
            pfx = "(?i)";
          else if(*fv == 16)
            pfx = "(?s)";
          else if(*fv == 18)
            pfx = "(?is)";
          else
            supported = false;
        }
        if(supported && !pfx.empty())
        {
          // The pattern (arg 0) must be a string literal or a Name bound to a
          // string constant; otherwise leave the call unchanged (nondet).
          const jsont &pat_node = *old_args.begin();
          std::optional<std::string> pat;
          if(is_node_type(pat_node, "Constant"))
          {
            const jsont &pv = json_member(pat_node, "value");
            if(pv.is_string())
              pat = pv.value;
          }
          else if(is_node_type(pat_node, "Name"))
          {
            auto si = string_constants.find(
              irep_idt{qualify_name(json_string(json_member(pat_node, "id")))});
            if(si != string_constants.end())
              pat = si->second;
          }
          if(pat.has_value())
          {
            // Build the modified call: prefixed literal pattern at arg 0, the
            // positional flags arg dropped (or the flags keyword removed).
            jsont modified = expr;
            json_objectt &mo = to_json_object(modified);
            json_arrayt new_args;
            std::size_t i = 0;
            for(const auto &a : old_args)
            {
              if(i == 0)
              {
                jsont pc = pat_node; // inherit source location fields
                json_objectt &pco = to_json_object(pc);
                pco["_type"] = json_stringt("Constant");
                pco["value"] = json_stringt(pfx + *pat);
                new_args.push_back(std::move(pc));
              }
              else if(!flags_is_kw && static_cast<int>(i) == flag_pos_idx)
              {
                // drop positional flags argument
              }
              else
                new_args.push_back(a);
              i++;
            }
            mo["args"] = new_args;
            if(flags_is_kw && re_kws.is_array())
            {
              json_arrayt new_kws;
              for(const auto &kw : as_array(re_kws))
                if(json_string(json_member(kw, "arg")) != "flags")
                  new_kws.push_back(kw);
              mo["keywords"] = new_kws;
            }
            return convert_call(modified);
          }
        }
      }
    }
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

  // Higher-order through containers (whole-group, PLR §6.13 / §4.2.2):
  // call a callable obtained by subscripting a statically-known
  // container — an inline list, a named list (list_literals), or a
  // named dict (dict_literals) whose elements are callables (functions,
  // lambdas, or comprehension closures). This covers fns[i](), d[k](),
  // and the late-binding comprehension `[lambda: i for i in range(n)]`.
  //
  // Each candidate is dispatched with its closure captures appended
  // (bound from closure_captures), so an escaping comprehension closure
  // observes the correct shared/final free-variable value. A constant
  // selector folds to a single candidate at solve time; a symbolic
  // selector becomes a guarded dispatch over the finite, statically
  // known element set (still decidable). When the container's elements
  // are not statically known the block does nothing and the callee
  // falls through to the sound nondet path.
  if(is_node_type(func, "Subscript"))
  {
    const jsont &cnode = json_member(func, "value");
    const jsont &slice = json_member(func, "slice");
    std::vector<std::pair<exprt, irep_idt>> cands;
    bool is_dict = false;
    bool ok = true;

    auto cid_from_expr = [&](const exprt &e) -> irep_idt
    {
      if(e.id() == ID_symbol && e.type().id() == ID_code)
        return to_symbol_expr(e).get_identifier();
      return irep_idt{};
    };
    auto cid_from_ast = [&](const jsont &v) -> irep_idt
    {
      if(is_node_type(v, "Lambda"))
      {
        exprt l = convert_lambda(v);
        if(l.id() == ID_symbol && l.type().id() == ID_code)
          return to_symbol_expr(l).get_identifier();
      }
      else if(is_node_type(v, "Name"))
      {
        std::string nm = json_string(json_member(v, "id"));
        auto ai = function_aliases.find(qualify_name(nm));
        if(ai != function_aliases.end())
          return ai->second;
        const symbolt *bs = symbol_table.lookup(irep_idt{"python::" + nm});
        if(bs != nullptr && bs->type.id() == ID_code)
          return bs->name;
      }
      return irep_idt{};
    };

    if(is_node_type(cnode, "List"))
    {
      const jsont &elts = json_member(cnode, "elts");
      if(elts.is_array() && !as_array(elts).empty())
      {
        std::size_t k = 0;
        for(const auto &el : as_array(elts))
        {
          irep_idt cid = cid_from_ast(el);
          if(cid.empty())
          {
            ok = false;
            break;
          }
          cands.push_back({from_integer(k++, python_int_type()), cid});
        }
      }
      else
        ok = false;
    }
    else if(is_node_type(cnode, "Name"))
    {
      irep_idt sid{qualify_name(json_string(json_member(cnode, "id")))};
      auto lit = list_literals.find(sid);
      auto dit = dict_literals.find(sid);
      if(
        lit != list_literals.end() && lit->second.id() == ID_struct &&
        lit->second.operands().size() >= 2)
      {
        const exprt &len_e = lit->second.operands()[0];
        const exprt &data = lit->second.operands()[1];
        mp_integer len_v;
        if(
          len_e.is_constant() && data.id() == ID_array &&
          !to_integer(to_constant_expr(len_e), len_v))
        {
          auto n = static_cast<std::size_t>(len_v.to_long());
          for(std::size_t k = 0; ok && k < n && k < data.operands().size(); ++k)
          {
            irep_idt cid = cid_from_expr(data.operands()[k]);
            if(cid.empty())
              ok = false;
            else
              cands.push_back({from_integer(k, python_int_type()), cid});
          }
        }
        else
          ok = false;
      }
      else if(
        dit != dict_literals.end() && dit->second.id() == ID_struct &&
        dit->second.operands().size() >= 3)
      {
        is_dict = true;
        const exprt &len_e = dit->second.operands()[0];
        const exprt &keys = dit->second.operands()[1];
        const exprt &vals = dit->second.operands()[2];
        mp_integer len_v;
        if(
          len_e.is_constant() && keys.id() == ID_array &&
          vals.id() == ID_array && !to_integer(to_constant_expr(len_e), len_v))
        {
          auto n = static_cast<std::size_t>(len_v.to_long());
          for(std::size_t k = 0; ok && k < n && k < keys.operands().size() &&
                                 k < vals.operands().size();
              ++k)
          {
            irep_idt cid = cid_from_expr(vals.operands()[k]);
            if(cid.empty())
              ok = false;
            else
              cands.push_back({keys.operands()[k], cid});
          }
        }
        else
          ok = false;
      }
      else
        ok = false;
    }
    else
      ok = false;

    if(ok && !cands.empty())
    {
      exprt::operandst call_args;
      bool args_ok = true;
      if(args.is_array())
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
      exprt sel = args_ok ? convert_expression(slice) : nil_exprt{};
      // All selectors must share the subscript's type to compare soundly.
      if(args_ok && sel.is_not_nil())
        for(const auto &c : cands)
          if(c.first.type() != sel.type())
          {
            args_ok = false;
            break;
          }
      if(args_ok && sel.is_not_nil())
      {
        // Build a call to `cid` with its closure captures appended.
        // Arguments are coerced to the callee's parameter types (an
        // unannotated parameter is a python_value, so a raw int must be
        // wrapped) — mirroring the normal call path, otherwise the
        // callee's dynamic-type checks fire spuriously.
        auto build_call = [&](const irep_idt &cid) -> exprt
        {
          const symbolt &cs = symbol_table.lookup_ref(cid);
          const auto &cparams = to_code_type(cs.type).parameters();
          exprt::operandst a2;
          for(std::size_t i = 0; i < call_args.size(); ++i)
          {
            exprt av = call_args[i];
            if(i < cparams.size())
            {
              const typet &pt = cparams[i].type();
              if(is_python_value_type(pt) && !is_python_value_type(av.type()))
                av = wrap_value(av);
              else if(av.type() != pt)
                av = safe_typecast(av, pt);
            }
            a2.push_back(av);
          }
          auto capi = closure_captures.find(id2string(cid));
          if(capi != closure_captures.end())
            for(const auto &[outer_id, name, type] : capi->second)
            {
              const symbolt *os = symbol_table.lookup(irep_idt{outer_id});
              if(os != nullptr)
                a2.push_back(os->symbol_expr());
              else
                a2.push_back(
                  side_effect_expr_nondett{type, get_location(expr)});
            }
          return side_effect_expr_function_callt{
            cs.symbol_expr(),
            a2,
            to_code_type(cs.type).return_type(),
            get_location(expr)};
        };

        typet ret_t =
          to_code_type(symbol_table.lookup_ref(cands[0].second).type)
            .return_type();
        if(ret_t.id() == ID_empty)
          ret_t = python_value_type();
        static unsigned cdisp_ctr = 0;
        std::string rn = "__cont_dispatch_" + std::to_string(cdisp_ctr++);
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

        std::vector<codet> disp;
        disp.push_back(code_frontend_assignt{
          result, side_effect_expr_nondett{ret_t, get_location(expr)}});
        exprt any_match = false_exprt{};
        for(const auto &[selv, cid] : cands)
        {
          equal_exprt eq{sel, selv};
          exprt cv = build_call(cid);
          if(cv.type() != ret_t)
            cv = typecast_exprt{std::move(cv), ret_t};
          code_blockt body;
          body.add(code_frontend_assignt{result, std::move(cv)});
          disp.push_back(code_ifthenelset{eq, std::move(body)});
          any_match = (any_match.id() == ID_false)
                        ? static_cast<exprt>(eq)
                        : static_cast<exprt>(or_exprt{any_match, eq});
        }
        for(auto &c : disp)
          pending_checks.push_back(std::move(c));
        // PLR §6.10: an index/key matching no element raises
        // IndexError (list) / KeyError (dict).
        emit_conditional_exception(
          not_exprt{any_match}, is_dict ? "KeyError" : "IndexError");
        return std::move(result);
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
    if(func_name == "Decimal" && args.is_array() && as_array(args).size() <= 1)
    {
      std::optional<std::tuple<long, long long, long, long, long>> parts;
      if(as_array(args).empty())
      {
        // Decimal() == Decimal(0).
        parts = std::make_tuple(0L, 0LL, 0L, 0L, 0L);
      }
      else
      {
        const jsont &arg0 = *as_array(args).begin();
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
        struct_exprt dval{std::move(fields), class_types.at("Decimal")};
        // Materialise into a temp lvalue so the result is addressable
        // (binary-operator dunder dispatch takes address_of(self), which
        // fails on a struct rvalue), mirroring the normal constructor path.
        static unsigned dec_tmp_counter = 0;
        std::string tn = "__dec_lit_" + std::to_string(dec_tmp_counter++);
        irep_idt tid{qualify_name(tn)};
        if(symbol_table.lookup(tid) == nullptr)
        {
          symbolt ts{tid, class_types.at("Decimal"), "python"};
          ts.base_name = tn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        symbol_exprt tsym = symbol_table.lookup_ref(tid).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{tsym, std::move(dval)});
        return std::move(tsym);
      }
    }

    // PLR §9: a "pure" subclass of `int`/`bool` (no own instance
    // attributes) behaves as that built-in, so model `U(v)` as the
    // underlying int value — `U(5) == 5`, `U(5) % 3`, etc. all work.
    // Subclasses that add instance state fall through to normal
    // instance construction below.
    {
      auto bit = class_bases.find(func_name);
      auto oit = class_owned_attrs.find(func_name);
      const bool no_own_attrs =
        (oit == class_owned_attrs.end() || oit->second.empty());
      if(bit != class_bases.end() && no_own_attrs)
      {
        bool int_base = false;
        for(const auto &b : bit->second)
          if(b == "int" || b == "bool")
            int_base = true;
        if(int_base)
        {
          if(args.is_array() && !as_array(args).empty())
          {
            exprt a = convert_expression(*as_array(args).begin());
            return safe_typecast(a, python_int_type());
          }
          return from_integer(0, python_int_type());
        }
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

    // Construct: __init__ call, or @dataclass field binding.
    for(auto &s : build_class_construction(
          func_name, tmp_sym.symbol_expr(), expr, get_location(expr)))
      pending_checks.push_back(std::move(s));

    return tmp_sym.symbol_expr();
  }
  // Fat-closure dispatch (doc/python-frontend-fat-closure-plan.md):
  // calling a Name that resolves to a python_value which may hold a
  // boxed closure. The dispatch is guarded on the CLOSURE tag, so a
  // non-closure value falls through to a sound nondet. Only attempted
  // when closures have actually been boxed (registry non-empty), and
  // only for a Name that resolves to a python_value variable/parameter
  // and is NOT a known function alias — otherwise converting the Name
  // here would be wrong (and could emit spurious checks).
  if(is_node_type(func, "Name") && !closure_registry.empty())
  {
    const std::string nm = json_string(json_member(func, "id"));
    const bool is_alias = function_aliases.count(qualify_name(nm)) > 0;
    const symbolt *vsym = nullptr;
    if(!is_alias)
    {
      if(!current_function.empty())
        vsym = symbol_table.lookup(
          irep_idt{"python::" + current_function + "::" + nm});
      if(vsym == nullptr)
        vsym = symbol_table.lookup(irep_idt{"python::" + nm});
    }
    if(
      vsym != nullptr && vsym->type.id() != ID_code &&
      is_python_value_type(vsym->type))
    {
      exprt callee = vsym->symbol_expr();
      exprt::operandst cargs;
      bool ok = true;
      if(args.is_array())
        for(const auto &a : as_array(args))
        {
          exprt ae = convert_expression(a);
          if(ae.is_nil())
          {
            ok = false;
            break;
          }
          cargs.push_back(ae);
        }
      if(ok)
      {
        exprt d = dispatch_closure_value(callee, cargs, get_location(expr));
        if(d.is_not_nil())
          return d;
      }
    }
  }

  // Calling a non-Name callee that evaluates to a python_value holding a
  // boxed closure / bound method (e.g. `handlers[0]()`, `d[k]()`). Route
  // through the closure dispatch (guarded on the CLOSURE tag, so a
  // non-closure value falls back to the sound nondet path).
  if(!closure_registry.empty() && is_node_type(func, "Subscript"))
  {
    exprt callee = convert_expression(func);
    if(callee.is_not_nil() && is_python_value_type(callee.type()))
    {
      exprt::operandst cargs;
      bool ok = true;
      if(args.is_array())
        for(const auto &a : as_array(args))
        {
          exprt ae = convert_expression(a);
          if(ae.is_nil())
          {
            ok = false;
            break;
          }
          cargs.push_back(ae);
        }
      if(ok)
      {
        exprt d = dispatch_closure_value(callee, cargs, get_location(expr));
        if(d.is_not_nil())
          return d;
      }
    }
  }

  // User-function-call fallback. Extracted to
  // python_converter_call_user.cpp for clarity. (Global value-tracking
  // invalidation for the post-call effect is done uniformly for ALL
  // call forms at the single Call chokepoint in convert_expression.)
  return convert_user_call(expr, func_name, args);
}
