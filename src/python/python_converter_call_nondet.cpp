/// Python to GOTO converter — verification-primitive call dispatch
/// (PLR §6.3.4). Handles nondet_*, __VERIFIER_nondet_*, randint,
/// the assume family, and the __cbmc_re_{match,search,fullmatch}
/// regex frontend hooks. Extracted from python_converter_call.cpp
/// per doc/python-frontend-architecture.md — pure
/// source-split; semantics are preserved.

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/json.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>

#include <solvers/strings/python_regex_to_smt.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"

#include <optional>
#include <string>

std::optional<exprt> python_convertert::try_nondet_call(
  const jsont &expr,
  const std::string &func_name,
  const jsont &args)
{
  if(func_name == "nondet_int" || func_name == "__VERIFIER_nondet_int")
  {
    side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
    return std::move(nondet);
  }
  // Frontend hooks for the Python library's re stub.
  // ``__cbmc_re_{match,search,fullmatch}(pattern, subject)`` are the
  // three entry points the library calls when both arguments are
  // Python strings. The front-end lowers each to the corresponding
  // ``cprover_string_{match,search,fullmatch}_func`` intrinsic; the
  // SMT backend (in particular ``--cvc5``) intercepts the intrinsic
  // and compiles the pattern to an SMT-LIB 2.6 regex term. When
  // the pattern cannot be translated (back-refs, lookaround, ...)
  // the intrinsic degrades to a sound nondet result.
  if(
    func_name == "__cbmc_re_match" || func_name == "__cbmc_re_search" ||
    func_name == "__cbmc_re_fullmatch")
  {
    if(args.is_array() && as_array(args).size() == 2)
    {
      auto it = as_array(args).begin();
      exprt pattern = convert_expression(*it++);
      exprt subject = convert_expression(*it);
      if(
        is_python_string_type(pattern.type()) &&
        is_python_string_type(subject.type()))
      {
        auto to_str = [](const exprt &s) -> exprt
        {
          // Native SMT-String back-end (Plan A): the regex intrinsics are
          // lowered with smt_string operands directly (str.in_re), so pass
          // the string through rather than wrapping it in a {length,data}
          // struct (which is malformed for smt_string).
          if(s.type().id() == ID_string)
            return s;
          if(s.id() == ID_struct && s.operands().size() == 2)
            return s;
          return struct_exprt{
            {member_exprt{s, "length", signedbv_typet{64}},
             member_exprt{s, "data", pointer_typet{unsignedbv_typet{8}, 64}}},
            s.type()};
        };
        irep_idt intrinsic_id = ID_cprover_string_match_func;
        if(func_name == "__cbmc_re_search")
          intrinsic_id = ID_cprover_string_search_func;
        else if(func_name == "__cbmc_re_fullmatch")
          intrinsic_id = ID_cprover_string_fullmatch_func;
        return emit_string_bool_function(
          intrinsic_id,
          to_str(pattern),
          to_str(subject),
          symbol_table,
          pending_checks);
      }
    }
    // Fallback: nondet bool.
    return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
  }
  else if(
    func_name == "__cbmc_re_search_start" ||
    func_name == "__cbmc_re_search_end")
  {
    // ``__cbmc_re_search_{start,end}(pattern, subject, from)`` -> int. The
    // leftmost match offset of `pattern` in `subject` at or after `from`
    // (start, resp. end = start + L), or -1 when there is no such match. The
    // SMT backend lowers it to a bounded leftmost-start scan, but ONLY for a
    // fixed-length translatable pattern on a constant subject (see
    // doc/python-frontend-regex-position-plan.md): otherwise it degrades to a
    // sound nondet int (the front-end here always emits the intrinsic; the
    // gating lives in smt2_conv, which falls back to the fresh nondet declared
    // in find_symbols). Match-or-None is decided separately by the bool
    // __cbmc_re_search, so an out-of-subset pattern keeps its precise
    // match decision and only loses position precision.
    //
    // The intrinsic is lowered only by the SMT-String backend (smt2_conv).
    // The refined-string solver has no axioms for it, so emitting the
    // function application there aborts in add_axioms_for_function_application.
    // On any non-native backend, return a sound nondet int directly.
    if(args.is_array() && as_array(args).size() == 3)
    {
      auto it = as_array(args).begin();
      exprt pattern = convert_expression(*it++);
      exprt subject = convert_expression(*it++);
      exprt from = convert_expression(*it);
      if(
        is_python_string_type(pattern.type()) &&
        is_python_string_type(subject.type()))
      {
        const irep_idt fn = func_name == "__cbmc_re_search_start"
                              ? ID_cprover_string_re_pos_start_func
                              : ID_cprover_string_re_pos_end_func;
        const exprt from64 = safe_typecast(from, signedbv_typet{64});
        if(use_smt_string_native)
          return native_string_app(
            fn,
            {pattern.type(), subject.type(), signedbv_typet{64}},
            {pattern, subject, from64},
            signedbv_typet{64});
        // Default (refined-string) backend: emit the intrinsic with the
        // {length,data} struct operands so the refined solver can constant-
        // fold it for a constant pattern+subject (otherwise it returns a sound
        // nondet int, no axioms). This is what makes re.findall / re.split
        // precise on the default backend without --python-smt-strings.
        auto to_str = [](const exprt &s) -> exprt
        {
          if(s.type().id() == ID_string)
            return s;
          if(s.id() == ID_struct && s.operands().size() == 2)
            return s;
          return struct_exprt{
            {member_exprt{s, "length", signedbv_typet{64}},
             member_exprt{s, "data", pointer_typet{unsignedbv_typet{8}, 64}}},
            s.type()};
        };
        const exprt ps = to_str(pattern);
        const exprt ss = to_str(subject);
        const irep_idt sym{fn};
        if(symbol_table.lookup(sym) == nullptr)
        {
          symbolt fs{
            sym,
            mathematical_function_typet(
              {ps.type(), ss.type(), signedbv_typet{64}}, signedbv_typet{64}),
            "python"};
          fs.base_name = id2string(fn);
          symbol_table.add(fs);
        }
        function_application_exprt app{
          symbol_table.lookup_ref(sym).symbol_expr(), {ps, ss, from64}};
        app.type() = signedbv_typet{64};
        const std::string rc = "__re_pos_" +
                               std::to_string(pending_checks.size()) + "_" +
                               std::to_string(symbol_table.symbols.size());
        const irep_idt rcid{"python::" + rc};
        if(symbol_table.lookup(rcid) == nullptr)
        {
          symbolt rs{rcid, signedbv_typet{64}, "python"};
          rs.base_name = rc;
          rs.is_lvalue = true;
          rs.is_state_var = true;
          symbol_table.add(rs);
        }
        pending_checks.push_back(code_frontend_assignt{
          symbol_table.lookup_ref(rcid).symbol_expr(), app});
        return symbol_table.lookup_ref(rcid).symbol_expr();
      }
    }
    // Fallback: nondet int (unrecognised operands).
    return side_effect_expr_nondett{signedbv_typet{64}, get_location(expr)};
  }
  else if(func_name == "__cbmc_re_sub")
  {
    // ``__cbmc_re_sub(pattern, repl, subject[, count])`` -> substituted
    // string. The native SMT-String backend lowers it to
    // (str.replace_re_all ...) only on the soundness subset (constant
    // fixed-length pattern, literal repl); otherwise smt2_conv emits a fresh
    // nondet String. str.replace_re_all replaces ALL occurrences, so a
    // bounded count (count != 0) is outside the precise model -- a
    // non-constant or non-zero count degrades here to a sound nondet string.
    const std::size_t n =
      args.is_array() ? as_array(args).size() : std::size_t{0};
    // The precise lowering is native-only: it builds an smt_string intrinsic
    // application, which is the wrong representation on the refined back-end
    // (and its string solver has no re_sub axioms). On refined, fall through
    // to the backend-aware bounded_nondet_string below.
    if(use_smt_string_native && (n == 3 || n == 4))
    {
      auto it = as_array(args).begin();
      exprt pattern = convert_expression(*it++);
      exprt repl = convert_expression(*it++);
      exprt subject = convert_expression(*it++);
      if(
        is_python_string_type(pattern.type()) &&
        is_python_string_type(repl.type()) &&
        is_python_string_type(subject.type()))
      {
        // Pass the operands with their actual types (smt_string for literals/
        // locals, the PYTHON_STRING_TAG struct for str-typed parameters);
        // smt2_conv's emit_smt_string coerces either form to an SMT String.
        exprt precise = native_string_app(
          ID_cprover_string_re_sub_func,
          {pattern.type(), repl.type(), subject.type()},
          {pattern, repl, subject},
          string_typet{});
        if(n == 3)
          return precise;
        // A bounded count (count != 0) is outside the precise model
        // (str.replace_re_all replaces ALL occurrences). Select the precise
        // result only when count == 0, otherwise a sound nondet string. The
        // count is typically a parameter (not a literal), so decide at
        // runtime rather than requiring a compile-time constant.
        exprt count = convert_expression(*it);
        exprt nondet = bounded_nondet_string(get_location(expr));
        exprt cnt_zero = equal_exprt{count, from_integer(0, count.type())};
        return if_exprt{std::move(cnt_zero), std::move(precise), nondet};
      }
    }
    // Default (refined-string) backend: emit re_sub so the refined solver can
    // constant-fold a replace-all (count==0) for a constant pattern/repl/
    // subject (otherwise a sound nondet string). count!=0 is outside the
    // precise (replace-all) model, so select it via the same count==0 guard.
    if(n == 3 || n == 4)
    {
      auto it = as_array(args).begin();
      exprt pattern = convert_expression(*it++);
      exprt repl = convert_expression(*it++);
      exprt subject = convert_expression(*it++);
      if(
        is_python_string_type(pattern.type()) &&
        is_python_string_type(repl.type()) &&
        is_python_string_type(subject.type()))
      {
        auto to_str = [](const exprt &s) -> exprt
        {
          if(s.type().id() == ID_string)
            return s;
          if(s.id() == ID_struct && s.operands().size() == 2)
            return s;
          return struct_exprt{
            {member_exprt{s, "length", signedbv_typet{64}},
             member_exprt{s, "data", pointer_typet{unsignedbv_typet{8}, 64}}},
            s.type()};
        };
        exprt precise = emit_string_function(
          ID_cprover_string_re_sub_func,
          {to_str(pattern), to_str(repl), to_str(subject)},
          symbol_table,
          pending_checks,
          loop_depth > 0,
          // global (documented): see emit_string_function -- scoping these
          // sites regressed refined-string precision (re-flags/github_2992);
          // they are candidates for the per-site scope audit.
          std::string{});
        if(n == 3)
          return precise;
        exprt count = convert_expression(*it);
        exprt nondet = bounded_nondet_string(get_location(expr));
        exprt cnt_zero = equal_exprt{count, from_integer(0, count.type())};
        return if_exprt{std::move(cnt_zero), std::move(precise), nondet};
      }
    }
    // Fallback: nondet string.
    return bounded_nondet_string(get_location(expr));
  }
  else if(func_name == "__cbmc_re_group")
  {
    // ``__cbmc_re_group(pattern, text, n)`` -> the text matched by the n-th
    // capture group of `pattern` within the whole-match text `text`. The
    // library stub calls this at match time to fill Match group slots;
    // Match.group(n) reads a slot. The native SMT-String backend lowers it to
    // a str.++ decomposition (smt2_conv); see
    // doc/python-frontend-regex-position-plan.md, Phase 3. The pattern is
    // recovered at conversion time in smt2_conv (constant propagation has run
    // by then), which the front-end cannot do for an imported-module stub
    // parameter -- hence the work happens in the backend, like the
    // match-position intrinsics. On any non-native backend, or when the
    // pattern/text are not usable, the result degrades to a sound nondet
    // string.
    if(use_smt_string_native && args.is_array() && as_array(args).size() == 3)
    {
      auto it = as_array(args).begin();
      exprt pattern = convert_expression(*it++);
      exprt text = convert_expression(*it++);
      exprt n = convert_expression(*it);
      if(
        is_python_string_type(pattern.type()) &&
        is_python_string_type(text.type()))
      {
        return native_string_app(
          ID_cprover_string_re_group_func,
          {pattern.type(), text.type(), signedbv_typet{64}},
          {pattern, text, safe_typecast(n, signedbv_typet{64})},
          string_typet{});
      }
    }
    // Fallback: a sound nondet string in the ACTIVE representation. Delegating
    // to nondet_str keeps this backend-aware -- crucially, on the refined
    // backend it yields a {length,data} struct, not an smt_string (which the
    // refined string solver cannot handle), so re.match/search/fullmatch stay
    // sound there rather than crashing.
    return try_nondet_call(expr, "nondet_str", json_arrayt{});
  }
  else if(func_name == "nondet_float" || func_name == "__VERIFIER_nondet_float")
  {
    side_effect_expr_nondett nondet{double_type(), get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "nondet_bool" || func_name == "__VERIFIER_nondet_bool")
  {
    side_effect_expr_nondett nondet{bool_typet{}, get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "randint")
  {
    // from random import randint — constrained nondet
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt lo = convert_expression(*it);
      ++it;
      exprt hi = convert_expression(*it);
      side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
      static unsigned ri_ctr = 0;
      std::string tn = "__randint_" + std::to_string(ri_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, python_int_type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{tv, nondet});
      pending_checks.push_back(
        code_assumet{binary_relation_exprt{tv, ID_ge, lo}});
      pending_checks.push_back(
        code_assumet{binary_relation_exprt{tv, ID_le, hi}});
      return tv;
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(func_name == "nondet_str" || func_name == "nondet_string")
  {
    static unsigned ns_ctr = 0;
    std::string tn = "__nondet_str_" + std::to_string(ns_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, python_string_type(), "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();

    if(use_smt_string_native)
    {
      // Native SMT-String back-end (Plan A): a free SMT String variable with a
      // bounded length (see bounded_nondet_string). When a size argument is
      // given, constrain the length to EXACTLY it — matching the refined
      // backend and the documented `nondet_string(N)` == length-exactly-N
      // semantics (other tests, e.g. `assert len(s) == 10`, depend on this).
      // Without this the native string was only bounded to [0, MAX], so
      // length-dependent asserts spuriously FAILED (e.g. s could be "").
      exprt s = bounded_nondet_string(get_location(expr));
      if(args.is_array() && !as_array(args).empty())
      {
        exprt size = convert_expression(*as_array(args).begin());
        exprt len = native_or_member_string_length(s);
        exprt size_t = safe_typecast(size, len.type());
        pending_checks.push_back(code_assumet{equal_exprt{len, size_t}});
      }
      return s;
    }

    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{python_string_type(), get_location(expr)}});
    // Emit the length constraint through the intrinsic so
    // downstream len() consumers (which route through
    // cprover_string_length_func) see the same value.
    exprt len_intr = emit_string_int_function(
      ID_cprover_string_length_func, tmp, symbol_table, pending_checks);
    // Bind the struct's length field to the solver's length so
    // downstream reads via `tmp.length` (e.g. the IndexError
    // out-of-range check in convert_subscript) see the same
    // value the solver constrains. Without this, `tmp.length`
    // is the nondet result of `tmp = nondet python_string` and
    // the OOB check fails on safe accesses.
    pending_checks.push_back(code_assumet{equal_exprt{
      member_exprt{tmp, "length", signedbv_typet{64}}, len_intr}});
    // If size argument provided, constrain length == size
    if(args.is_array() && !as_array(args).empty())
    {
      exprt size = convert_expression(*as_array(args).begin());
      exprt size_i64 = safe_typecast(size, signedbv_typet{64});
      pending_checks.push_back(code_assumet{equal_exprt{len_intr, size_i64}});
    }
    else
    {
      // ESBMC compatibility: --nondet-str-length defaults to 16
      // (one char reserved for the implicit terminator), so the
      // visible length is in [0, 15]. Bound likewise so tests
      // that assert `len(s) < 16` after a default-call to
      // nondet_str() pass.
      pending_checks.push_back(code_assumet{and_exprt{
        binary_relation_exprt{
          len_intr, ID_ge, from_integer(0, signedbv_typet{64})},
        binary_relation_exprt{
          len_intr, ID_le, from_integer(15, signedbv_typet{64})}}});
    }
    return std::move(tmp);
  }
  else if(func_name == "nondet_list")
  {
    // nondet_list(n[, sample]) — constrain length to [0, n] and
    // build a list whose element type matches the optional
    // second positional sample expression. The sample can be
    // any expression with a recognised concrete type (typical
    // usage: nondet_list(8, nondet_int()) / nondet_bool() /
    // nondet_float() / nondet_str()). Default size matches
    // ESBMC's --nondet-list-length=8; default element type is
    // python_int.
    static unsigned nl_ctr = 0;
    exprt max_len = from_integer(8, signedbv_typet{64});
    typet elem_type = python_int_type();
    if(args.is_array())
    {
      auto it = as_array(args).begin();
      auto end = as_array(args).end();
      if(it != end)
      {
        exprt arg = convert_expression(*it);
        if(arg.type().id() != ID_signedbv)
          arg = safe_typecast(arg, signedbv_typet{64});
        max_len = arg;
        ++it;
      }
      if(it != end)
      {
        exprt sample = convert_expression(*it);
        if(!sample.is_nil() && sample.type().id() != ID_empty)
          elem_type = sample.type();
      }
    }
    std::string tn = "__nondet_list_" + std::to_string(nl_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    typet lt = python_list_type(elem_type);
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, lt, "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{lt, get_location(expr)}});
    member_exprt len{tmp, "length", signedbv_typet{64}};
    pending_checks.push_back(code_assumet{and_exprt{
      binary_relation_exprt{len, ID_ge, from_integer(0, signedbv_typet{64})},
      binary_relation_exprt{len, ID_le, max_len}}});
    return std::move(tmp);
  }
  else if(func_name == "nondet_bytes")
  {
    // bytes is modelled as a list of unsigned 8-bit ints (see
    // convert_type_annotation). A sound nondet bytes value is a bounded list
    // of nondet u8 -- used by stubs whose bytes result is value-dependent
    // (io.read / socket.recv) instead of a fixed b"" (which false-proves
    // `read() == b""`). Optional first arg bounds the length to [0, N]
    // (default 8, matching nondet_list).
    static unsigned nb_ctr = 0;
    exprt max_len = from_integer(8, signedbv_typet{64});
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(arg.type().id() != ID_signedbv)
        arg = safe_typecast(arg, signedbv_typet{64});
      max_len = arg;
    }
    const typet bt = python_list_type(unsignedbv_typet{8});
    std::string tn = "__nondet_bytes_" + std::to_string(nb_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, bt, "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{bt, get_location(expr)}});
    member_exprt len{tmp, "length", signedbv_typet{64}};
    pending_checks.push_back(code_assumet{and_exprt{
      binary_relation_exprt{len, ID_ge, from_integer(0, signedbv_typet{64})},
      binary_relation_exprt{len, ID_le, max_len}}});
    return std::move(tmp);
  }
  else if(func_name == "nondet_dict")
  {
    // nondet_dict(n[, key_type=K, value_type=V]) — constrain
    // length to [0, n] and use the keyword arguments' types
    // for the key/value array element types when present.
    // Default size matches ESBMC's --nondet-dict-length=8;
    // default key type is python_string, default value type
    // is python_int.
    static unsigned nd_ctr = 0;
    exprt max_len = from_integer(8, signedbv_typet{64});
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(arg.type().id() != ID_signedbv)
        arg = safe_typecast(arg, signedbv_typet{64});
      max_len = arg;
    }
    typet key_type = python_string_type();
    typet val_type = python_int_type();
    const jsont &kwargs = json_member(expr, "keywords");
    if(kwargs.is_array())
    {
      for(const auto &kw : as_array(kwargs))
      {
        std::string kn = json_string(json_member(kw, "arg"));
        if(kn != "key_type" && kn != "value_type")
          continue;
        exprt v = convert_expression(json_member(kw, "value"));
        if(v.is_nil() || v.type().id() == ID_empty)
          continue;
        if(kn == "key_type")
          key_type = v.type();
        else
          val_type = v.type();
      }
    }
    typet dt = python_dict_type(key_type, val_type);
    std::string tn = "__nondet_dict_" + std::to_string(nd_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, dt, "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{dt, get_location(expr)}});
    member_exprt len{tmp, "length", signedbv_typet{64}};
    pending_checks.push_back(code_assumet{and_exprt{
      binary_relation_exprt{len, ID_ge, from_integer(0, signedbv_typet{64})},
      binary_relation_exprt{len, ID_le, max_len}}});
    return std::move(tmp);
  }
  else if(func_name == "nondet_complex")
  {
    struct_typet::componentst comps;
    comps.push_back(struct_typet::componentt{"real", double_type()});
    comps.push_back(struct_typet::componentt{"imag", double_type()});
    struct_typet ct{comps};
    ct.set_tag("python_complex");
    return side_effect_expr_nondett{ct, get_location(expr)};
  }
  // PLR: assume() constrains nondet values (ESBMC/CBMC verification primitive)
  else if(
    func_name == "assume" || func_name == "__VERIFIER_assume" ||
    func_name == "__CPROVER_assume" || func_name == "__ESBMC_assume")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      const jsont &a0 = *as_array(args).begin();
      // QUANTIFIED assume: assume(all(<pred> for v1 in range(R1)
      // [for v2 in range(R2)])) lowers to a (nested) FORALL assume
      // -- the Python-source form of __CPROVER_assume(forall ...),
      // the contract channel stub docstrings cannot provide (a
      // docstring is not semantics; the sortedness contract of the
      // study's ex3 needs stating as CODE). Bounds may read
      // len(...) or the outer binder (triangular ranges). The
      // predicate must be a pure quantifier-safe term; anything
      // else falls through to the plain path (which loud-fails on
      // the genexp, never silently weakens).
      if(is_node_type(a0, "Call"))
      {
        const jsont &af = json_member(a0, "func");
        if(
          is_node_type(af, "Name") &&
          json_string(json_member(af, "id")) == "all")
        {
          const jsont &aargs = json_member(a0, "args");
          if(aargs.is_array() && as_array(aargs).size() == 1)
          {
            exprt q = try_quantified_all(*as_array(aargs).begin());
            if(!q.is_nil())
            {
              pending_checks.push_back(code_assumet{std::move(q)});
              return from_integer(0, python_int_type());
            }
          }
        }
      }
      exprt cond = convert_expression(a0);
      if(cond.type().id() != ID_bool)
        cond = typecast_exprt{cond, bool_typet{}};
      pending_checks.push_back(code_assumet{cond});
    }
    return from_integer(0, python_int_type());
  }

  return std::nullopt;
}
