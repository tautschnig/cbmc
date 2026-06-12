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
          if(s.type().id() == ID_smt_string)
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
      // Native SMT-String back-end (Plan A): a free SMT String variable.
      static unsigned nsn_ctr = 0;
      std::string nm = "__nondet_smtstr_" + std::to_string(nsn_ctr++);
      irep_idt nid{qualify_name(nm)};
      if(symbol_table.lookup(nid) == nullptr)
      {
        symbolt s{nid, smt_string_typet{}, "python"};
        s.base_name = nm;
        s.is_lvalue = true;
        s.is_state_var = true;
        symbol_table.add(s);
      }
      symbol_exprt t = symbol_table.lookup_ref(nid).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{
        t, side_effect_expr_nondett{smt_string_typet{}, get_location(expr)}});
      return std::move(t);
    }

    if(use_smt_string_backend)
    {
      // SMT-String back-end: give the leaf a concrete backing array so its
      // data pointer resolves (via symex value-substitution) to
      // address_of(index(backing, 0)) -- the shape smt2_conv's str.*
      // lowering recognises. This makes symbol-operand membership/ordering
      // precise under an SMT String solver (--cvc5), reusing the same
      // content-pointer-resolution idea as the refined back-end's Phase 1/2.
      // The backing size matches smt2_conv's fixed unroll bound so every
      // byte index it emits is in-bounds.
      const std::size_t bound = PYTHON_MAX_STRING_LENGTH;
      array_typet at{
        unsignedbv_typet{8}, from_integer(bound, signedbv_typet{64})};
      auto make_sym = [&](const std::string &base, const typet &t)
      {
        std::string nm = base + std::to_string(ns_ctr - 1);
        irep_idt id{qualify_name(nm)};
        if(symbol_table.lookup(id) == nullptr)
        {
          symbolt s{id, t, "python"};
          s.base_name = nm;
          s.is_lvalue = true;
          s.is_state_var = true;
          symbol_table.add(s);
        }
        return symbol_table.lookup_ref(id).symbol_expr();
      };
      symbol_exprt backing = make_sym("__nondet_str_data_", at);
      symbol_exprt len_sym = make_sym("__nondet_str_len_", signedbv_typet{64});
      exprt content = address_of_exprt(index_exprt(
        backing, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
      pending_checks.push_back(code_frontend_assignt{
        tmp, struct_exprt({len_sym, content}, python_string_type())});
      // Constrain the length: == size when given, else the default range;
      // always within the backing bound.
      if(args.is_array() && !as_array(args).empty())
      {
        exprt size_i64 = safe_typecast(
          convert_expression(*as_array(args).begin()), signedbv_typet{64});
        pending_checks.push_back(code_assumet{equal_exprt{len_sym, size_i64}});
      }
      else
      {
        pending_checks.push_back(code_assumet{and_exprt{
          binary_relation_exprt{
            len_sym, ID_ge, from_integer(0, signedbv_typet{64})},
          binary_relation_exprt{
            len_sym, ID_le, from_integer(15, signedbv_typet{64})}}});
      }
      pending_checks.push_back(code_assumet{binary_relation_exprt{
        len_sym, ID_le, from_integer(bound, signedbv_typet{64})}});
      return std::move(tmp);
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
      exprt cond = convert_expression(*as_array(args).begin());
      if(cond.type().id() != ID_bool)
        cond = typecast_exprt{cond, bool_typet{}};
      pending_checks.push_back(code_assumet{cond});
    }
    return from_integer(0, python_int_type());
  }

  return std::nullopt;
}
