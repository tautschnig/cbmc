/// Python to GOTO converter — ListComp (PLR §6.2.4) and
/// DictComp (PLR §6.2.6) implementation.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/json.h>
#include <util/namespace.h>
#include <util/replace_expr.h>
#include <util/simplify_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

// §12c: lower `[elt for var in iter_list (if ...)*]` to a GOTO loop.
exprt python_convertert::emit_listcomp_loop(
  const jsont &elt,
  const std::string &var_name,
  const exprt &iter_list,
  const jsont &ifs,
  const source_locationt &loc)
{
  const struct_typet &ist = to_struct_type(iter_list.type());
  const array_typet &idata_t = to_array_type(ist.components()[1].type());
  typet elem_in_t = idata_t.element_type();
  typet len_t = signedbv_typet{64};

  // Loop variable symbol (qualified into the enclosing scope, matching
  // the converter's existing comprehension-variable handling) so the
  // element expression's references to it resolve.
  irep_idt var_id{qualify_name(var_name)};
  if(symbol_table.lookup(var_id) == nullptr)
  {
    symbolt vs{var_id, elem_in_t, "python"};
    vs.base_name = var_name;
    vs.is_lvalue = true;
    vs.is_state_var = true;
    vs.is_static_lifetime = current_function.empty();
    symbol_table.add(vs);
  }
  else
    symbol_table.get_writeable_ref(var_id).type = elem_in_t;
  symbol_exprt var = symbol_table.lookup_ref(var_id).symbol_expr();

  // Convert the element expression (and any filter conditions) with the
  // loop variable in scope, capturing their checks so they land INSIDE
  // the loop body (guarded by the iteration) rather than at the
  // comprehension site.
  std::vector<codet> saved;
  saved.swap(pending_checks);
  exprt elt_val = convert_expression(elt);
  std::vector<codet> elt_checks;
  elt_checks.swap(pending_checks);
  exprt cond = true_exprt{};
  std::vector<codet> cond_checks;
  if(ifs.is_array())
    for(const auto &c : as_array(ifs))
    {
      exprt cv = convert_expression(c);
      std::vector<codet> cc;
      cc.swap(pending_checks);
      for(auto &s : cc)
        cond_checks.push_back(std::move(s));
      if(!cv.is_nil())
        cond = (cond == true_exprt{})
                 ? safe_typecast(cv, bool_typet{})
                 : exprt{and_exprt{cond, safe_typecast(cv, bool_typet{})}};
    }
  saved.swap(pending_checks);
  if(elt_val.is_nil())
    return nil_exprt{};

  typet et_out = elt_val.type();
  struct_typet list_type = python_list_type(et_out);
  const array_typet &rdata_t = to_array_type(list_type.components()[1].type());

  // Result list + source-index + result-count temporaries.
  static unsigned lc_ctr = 0;
  auto mk = [&](const std::string &base, const typet &t) -> symbol_exprt
  {
    irep_idt id{qualify_name(base + std::to_string(lc_ctr))};
    if(symbol_table.lookup(id) == nullptr)
    {
      symbolt s{id, t, "python"};
      s.base_name = base + std::to_string(lc_ctr);
      s.is_lvalue = true;
      s.is_state_var = true;
      s.is_static_lifetime = current_function.empty();
      symbol_table.add(s);
    }
    return symbol_table.lookup_ref(id).symbol_expr();
  };
  symbol_exprt result = mk("__listcomp_", list_type);
  symbol_exprt si = mk("__listcomp_i_", len_t);
  symbol_exprt ni = mk("__listcomp_n_", len_t);

  member_exprt iter_len{iter_list, "length", len_t};
  member_exprt iter_data{iter_list, "data", idata_t};
  member_exprt res_len{result, "length", len_t};
  member_exprt res_data{result, "data", rdata_t};

  // Closed-form MAP encoding (--python-smt-containers): a
  // filter-free single-generator comprehension whose element
  // expression is PURE (its conversion emitted no checks: no
  // may-raise operations, no side-effecting calls) is a MAP --
  // result.length == iter.length and
  // result.data == (lambda j. elt[var := iter.data[j]]),
  // expressed with array_comprehension_exprt. This is EXACT at any
  // (symbolic) length: no loop, no unwinding, no capacity bound --
  // the loop lowering below would need iter.length unwindings and
  // silently truncates a symbolic-length iterable at the unwind
  // bound (an unwinding-assertion failure / false alarm). The
  // backends handle it as (lambda ...) (z3) or a universally
  // quantified array axiom (generic smt2, cvc5); the simplifier
  // folds constant-index reads by substitution, so concrete tests
  // keep folding at symex time.
  if(
    python_smt_containers_flag() && cond == true_exprt{} &&
    elt_checks.empty() && cond_checks.empty() && quantifier_safe_term(elt_val))
  {
    static unsigned cf_ctr = 0;
    const std::string jn = "__comp_j_" + std::to_string(cf_ctr++);
    const irep_idt jid{qualify_name(jn)};
    if(symbol_table.lookup(jid) == nullptr)
    {
      // The bound variable must be a REGISTERED symbol: symex renames
      // binder and body occurrences consistently through the L0/L1/L2
      // levels (the same route the C frontend's quantifier bound
      // variables take), and renaming requires table presence.
      symbolt js{jid, len_t, "python"};
      js.base_name = jn;
      js.is_lvalue = true;
      js.is_state_var = true;
      js.is_static_lifetime = current_function.empty();
      symbol_table.add(js);
    }
    const symbol_exprt j = symbol_table.lookup_ref(jid).symbol_expr();
    exprt slot = index_exprt{iter_data, j};
    exprt bound =
      (slot.type() != elem_in_t) ? safe_typecast(slot, elem_in_t) : slot;
    exprt body = elt_val;
    replace_expr(var, bound, body);
    if(body.type() != et_out)
      body = safe_typecast(body, et_out);
    lc_ctr++;
    pending_checks.push_back(code_frontend_assignt{
      result,
      struct_exprt{
        {iter_len, array_comprehension_exprt{j, std::move(body), rdata_t}},
        list_type}});
    return std::move(result);
  }
  lc_ctr++;

  pending_checks.push_back(code_frontend_assignt{result, safe_zero(list_type)});
  pending_checks.push_back(code_frontend_assignt{si, from_integer(0, len_t)});
  pending_checks.push_back(code_frontend_assignt{ni, from_integer(0, len_t)});

  // Loop body: var = iter.data[si]; <elt checks>; if(cond) { guard;
  // result.data[ni] = elt; ni += 1 } si += 1
  code_blockt body;
  {
    exprt slot = index_exprt{iter_data, si};
    exprt bound =
      (slot.type() != elem_in_t) ? safe_typecast(slot, elem_in_t) : slot;
    body.add(code_frontend_assignt{var, bound});
    for(auto &s : elt_checks)
      body.add(std::move(s));
    for(auto &s : cond_checks)
      body.add(std::move(s));
    code_blockt store;
    emit_capacity_guard(store, ni, PYTHON_MAX_LIST_LENGTH, loc, true);
    exprt sval =
      (elt_val.type() != et_out) ? safe_typecast(elt_val, et_out) : elt_val;
    store.add(code_frontend_assignt{index_exprt{res_data, ni}, sval});
    store.add(
      code_frontend_assignt{ni, plus_exprt{ni, from_integer(1, len_t)}});
    if(cond == true_exprt{})
      for(auto &s : store.statements())
        body.add(std::move(s));
    else
      body.add(code_ifthenelset{cond, std::move(store)});
    body.add(code_frontend_assignt{si, plus_exprt{si, from_integer(1, len_t)}});
  }
  pending_checks.push_back(
    code_whilet{binary_relation_exprt{si, ID_lt, iter_len}, std::move(body)});
  pending_checks.push_back(code_frontend_assignt{res_len, ni});
  return std::move(result);
}

// "A comprehension consists of a single expression followed by at least
// one for clause and zero or more for or if clauses."
exprt python_convertert::convert_list_comp(const jsont &expr)
{
  const jsont &elt = json_member(expr, "elt");
  const jsont &generators = json_member(expr, "generators");

  if(!generators.is_array() || as_array(generators).empty())
    return nil_exprt{};

  // Nested comprehension: an element expression that itself contains a
  // comprehension (e.g. `[len([j for j in range(i)]) for i in range(n)]`) is
  // not correctly re-evaluated per OUTER iteration by the unroll path -- the
  // inner comp's outer-var-dependent iterable produced definite-WRONG element
  // values (a false proof found by the mutation-oracle). Over-approximate
  // soundly with a nondet list until nested comps are precisely modelled.
  {
    std::function<bool(const jsont &)> has_comp = [&](const jsont &n) -> bool
    {
      if(
        is_node_type(n, "ListComp") || is_node_type(n, "SetComp") ||
        is_node_type(n, "DictComp") || is_node_type(n, "GeneratorExp"))
        return true;
      if(n.is_array())
      {
        for(const auto &e : as_array(n))
          if(has_comp(e))
            return true;
      }
      else if(n.is_object())
      {
        for(const auto &kv : to_json_object(n))
          if(has_comp(kv.second))
            return true;
      }
      return false;
    };
    if(has_comp(elt))
      return side_effect_expr_nondett{
        python_list_type(python_value_type()), get_location(expr)};
  }

  // §12c: single generator over a runtime list whose length is
  // symbolic (a `list` parameter, or a call/expression yielding a
  // non-constant-length list). The unroll path below only supports
  // compile-time-enumerable iterables and otherwise drops the
  // assignment; lower these to a real loop populating a temporary.
  // Excludes literal-list / range() / a Name tracked as a literal
  // list, which the unroll path handles without needing --unwind.
  if(as_array(generators).size() == 1)
  {
    const jsont &gen = *as_array(generators).begin();
    const jsont &gen_iter = json_member(gen, "iter");
    const jsont &target = json_member(gen, "target");
    bool is_range_call =
      is_node_type(gen_iter, "Call") &&
      is_node_type(json_member(gen_iter, "func"), "Name") &&
      json_string(json_member(json_member(gen_iter, "func"), "id")) == "range";
    bool is_tracked_name =
      is_node_type(gen_iter, "Name") &&
      list_literals.count(
        irep_idt{qualify_name(json_string(json_member(gen_iter, "id")))}) > 0;
    // Closed-form PRODUCERS (comprehension-closedform plan, P3
    // residual; --python-smt-containers): a filter-free
    // tuple-unpacking comprehension over enumerate()/zip() is a
    // MULTI-SOURCE MAP -- no tuple is ever materialized, each
    // unpacked name is substituted with its index-wise source
    // expression:
    //   [body for i, v in enumerate(xs, start)]
    //     -> {len(xs), array_comprehension j. body[i := j+start,
    //                                             v := xs.data[j]]}
    //   [body for x, y in zip(a, b)]
    //     -> {min(la, lb), array_comprehension j. body[x := a.data[j],
    //                                                  y := b.data[j]]}
    // (zip stops at the shortest input per PLR 5.8 -- the min length
    // is exact, and enumerate's start offset rides along.) The same
    // purity gate as the other closed forms; ineligible shapes keep
    // the existing lowering.
    const jsont &gen_ifs_cf = json_member(gen, "ifs");
    const bool no_filters_cf =
      !gen_ifs_cf.is_array() || as_array(gen_ifs_cf).empty();
    if(
      python_smt_containers_flag() && no_filters_cf &&
      is_node_type(target, "Tuple") && is_node_type(gen_iter, "Call") &&
      is_node_type(json_member(gen_iter, "func"), "Name"))
    {
      const std::string fn =
        json_string(json_member(json_member(gen_iter, "func"), "id"));
      const jsont &telts = json_member(target, "elts");
      std::vector<std::string> names;
      if(telts.is_array())
        for(const auto &t : as_array(telts))
        {
          if(!is_node_type(t, "Name"))
          {
            names.clear();
            break;
          }
          names.push_back(json_string(json_member(t, "id")));
        }
      const jsont &cargs = json_member(gen_iter, "args");
      const std::size_t n_args = cargs.is_array() ? as_array(cargs).size() : 0;
      const bool is_enum =
        fn == "enumerate" && names.size() == 2 && (n_args == 1 || n_args == 2);
      const bool is_zip =
        fn == "zip" && names.size() == n_args && (n_args == 2 || n_args == 3);
      if(is_enum || is_zip)
      {
        // Convert the sources; every source must yield an iteration
        // view (a list, possibly boxed).
        std::vector<codet> saved_cf;
        saved_cf.swap(pending_checks);
        std::vector<iteration_viewt> views;
        exprt start_ofs = from_integer(0, signedbv_typet{64});
        bool ok = true;
        auto it_arg = as_array(cargs).begin();
        const std::size_t n_sources = is_enum ? 1 : n_args;
        for(std::size_t k = 0; k < n_sources && ok; ++k, ++it_arg)
        {
          exprt src = convert_expression(*it_arg);
          auto v = make_iteration_view(src);
          if(v.has_value())
            views.push_back(std::move(*v));
          else
            ok = false;
        }
        if(is_enum && ok && n_args == 2)
        {
          exprt se = convert_expression(*it_arg);
          if(se.type() != signedbv_typet{64})
            se = safe_typecast(se, signedbv_typet{64});
          start_ofs = std::move(se);
        }
        // The source conversions themselves must be pure, too.
        ok = ok && pending_checks.empty();
        if(ok)
        {
          // Bind the unpacked names: enumerate -> (int64, elem);
          // zip -> one name per source element type.
          std::vector<symbol_exprt> name_syms;
          for(std::size_t k = 0; k < names.size(); ++k)
          {
            const typet nt = (is_enum && k == 0)
                               ? typet{signedbv_typet{64}}
                               : views[is_enum ? 0 : k].element_type;
            irep_idt nid{qualify_name(names[k])};
            if(symbol_table.lookup(nid) == nullptr)
            {
              symbolt ns_{nid, nt, "python"};
              ns_.base_name = names[k];
              ns_.is_lvalue = true;
              ns_.is_state_var = true;
              ns_.is_static_lifetime = current_function.empty();
              symbol_table.add(ns_);
            }
            else
              symbol_table.get_writeable_ref(nid).type = nt;
            name_syms.push_back(symbol_table.lookup_ref(nid).symbol_expr());
          }
          exprt elt_val_cf = convert_expression(elt);
          if(
            pending_checks.empty() && !elt_val_cf.is_nil() &&
            quantifier_safe_term(elt_val_cf))
          {
            symbol_exprt j = fresh_bound_index("__prod_j_");
            if(is_enum)
            {
              exprt iv = plus_exprt{j, start_ofs};
              replace_expr(name_syms[0], iv, elt_val_cf);
              replace_expr(name_syms[1], views[0].elem(j), elt_val_cf);
            }
            else
            {
              for(std::size_t k = 0; k < names.size(); ++k)
                replace_expr(name_syms[k], views[k].elem(j), elt_val_cf);
            }
            exprt length = views[0].length;
            for(std::size_t k = 1; k < views.size(); ++k)
              length = if_exprt{
                binary_relation_exprt{views[k].length, ID_lt, length},
                views[k].length,
                length};
            const typet et_out_cf = elt_val_cf.type();
            struct_typet lt_cf = python_list_type(et_out_cf);
            const array_typet &rd_cf =
              to_array_type(lt_cf.components()[1].type());
            saved_cf.swap(pending_checks);
            static unsigned prod_ctr = 0;
            irep_idt rid{
              qualify_name("__prodcomp_" + std::to_string(prod_ctr++))};
            if(symbol_table.lookup(rid) == nullptr)
            {
              symbolt rs{rid, lt_cf, "python"};
              rs.base_name = id2string(rid);
              rs.is_lvalue = true;
              rs.is_state_var = true;
              rs.is_static_lifetime = current_function.empty();
              symbol_table.add(rs);
            }
            symbol_exprt result_cf = symbol_table.lookup_ref(rid).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{
              result_cf,
              struct_exprt{
                {std::move(length),
                 array_comprehension_exprt{j, std::move(elt_val_cf), rd_cf}},
                lt_cf}});
            return std::move(result_cf);
          }
        }
        // ineligible: discard trial conversions, restore, fall through
        pending_checks.clear();
        saved_cf.swap(pending_checks);
      }
      else
        (void)0;
    }

    if(
      is_node_type(target, "Name") && !is_node_type(gen_iter, "List") &&
      !is_range_call && !is_tracked_name)
    {
      // Convert the iterable once, capturing its checks so a
      // fall-through (non-list / constant-length) doesn't leave them
      // emitted twice; the unroll path re-converts as needed.
      std::vector<codet> saved;
      saved.swap(pending_checks);
      exprt iter_val = convert_expression(gen_iter);
      std::vector<codet> iter_checks;
      iter_checks.swap(pending_checks);
      saved.swap(pending_checks);
      if(iter_val.type().id() == ID_pointer)
        iter_val = dereference_exprt{iter_val};

      // PLR §3.3.1: a comprehension over a class instance whose MRO defines
      // neither __iter__ nor __getitem__ is not iterable -> TypeError (mirrors
      // the for-loop check). Gated identically so a __getitem__-only sequence
      // class is not flagged.
      {
        // PLR §3.3.1: a provably non-iterable SCALAR (int/float/bool/complex/
        // None) as the comprehension source raises TypeError (`[x for x in 5]`).
        // Shared predicate with the for-loop / unpack sites; PROVABLE scalars
        // only (Any / str / containers never fire).
        if(provably_non_iterable_scalar(iter_val))
          emit_conditional_exception(true_exprt{}, "TypeError");
        std::string itag;
        if(iter_val.type().id() == ID_struct)
          itag = id2string(to_struct_type(iter_val.type()).get_tag());
        else if(iter_val.type().id() == ID_struct_tag)
          itag =
            id2string(to_struct_tag_type(iter_val.type()).get_identifier());
        if(
          itag.substr(0, 13) == "python_class_" &&
          !class_mro_defines(itag.substr(13), "__iter__") &&
          !class_mro_defines(itag.substr(13), "__getitem__"))
        {
          emit_conditional_exception(true_exprt{}, "TypeError");
        }
      }
      // Shared pv-iterable lowering (iterability obligation + identity-
      // refined __iter__ dispatch) -- the same whole-group as the for-loop:
      // a comprehension over an Any-held CLASS instance previously iterated
      // the garbage list slot (wholly-nondet elements, false alarms) with NO
      // iterability obligation (false proof for an Any-held int). Runs
      // AFTER the provably-non-iterable-scalar check above: lowering
      // replaces iter_val with the list view, which would MASK the
      // constant-None detection (a regression the noniterable-comprehension
      // test caught).
      if(is_python_value_type(iter_val.type()))
      {
        code_blockt pv_header;
        iter_val = lower_pv_iterable(iter_val, pv_header, get_location(expr));
        for(auto &st : pv_header.statements())
          iter_checks.push_back(std::move(st));
      }
      bool const_len = iter_val.id() == ID_struct &&
                       !iter_val.operands().empty() &&
                       iter_val.operands()[0].is_constant();
      if(
        !iter_val.is_nil() && is_python_list_type(iter_val.type()) &&
        !const_len)
      {
        for(auto &c : iter_checks)
          pending_checks.push_back(std::move(c));
        exprt r = emit_listcomp_loop(
          elt,
          json_string(json_member(target, "id")),
          iter_val,
          json_member(gen, "ifs"),
          get_location(expr));
        if(!r.is_nil())
          return r;
      }
    }
  }

  // Collect all generators (support nested for)
  struct gen_info
  {
    std::string var_name;
    std::vector<const jsont *> values;
    std::vector<exprt> const_values;
  };
  std::vector<gen_info> gens;

  for(const auto &gen : as_array(generators))
  {
    const jsont &gen_iter = json_member(gen, "iter");
    gen_info gi;
    gi.var_name = json_string(json_member(json_member(gen, "target"), "id"));
    // Literal-list form: [f(x) for x in [1, 2, 3]]
    if(is_node_type(gen_iter, "List"))
    {
      const jsont &elts = json_member(gen_iter, "elts");
      if(elts.is_array())
      {
        for(const auto &e : as_array(elts))
          gi.values.push_back(&e);
      }
    }
    // Name form: xs = [1, 2, 3]; [f(x) for x in xs]
    // Resolve via list_literals if the name points to a
    // tracked literal list.
    else if(is_node_type(gen_iter, "Name"))
    {
      std::string iter_name = json_string(json_member(gen_iter, "id"));
      auto it = list_literals.find(irep_idt{qualify_name(iter_name)});
      if(it != list_literals.end())
      {
        // The list_literal is a struct_exprt with [length,
        // data]. data is an array_exprt whose operands are
        // the values. But we need JSON pointers for the same
        // values — we can't plug non-JSON exprt values into
        // the gens structure.
        // Build synthetic JSON Constant nodes? Too complex.
        // Instead, just take the values directly into
        // a secondary 'const_values' stream.
        const exprt &list_struct = it->second;
        if(list_struct.operands().size() >= 2)
        {
          const exprt &length_expr = list_struct.operands()[0];
          if(length_expr.is_constant())
          {
            mp_integer len;
            if(!to_integer(to_constant_expr(length_expr), len))
            {
              const exprt &data_arr = list_struct.operands()[1];
              for(mp_integer i = 0; i < len; ++i)
              {
                auto el = list_literal_element(data_arr, i.to_ulong());
                if(el.has_value())
                  gi.const_values.push_back(std::move(*el));
              }
            }
          }
        }
      }
      if(gi.values.empty() && gi.const_values.empty())
      {
        log.warning() << "List comprehension iterable '" << iter_name
                      << "' is not a known literal list" << messaget::eom;
        return nil_exprt{};
      }
    }
    // Range form: [f(x) for x in range(N)] or range(a, b[, c])
    else if(
      is_node_type(gen_iter, "Call") &&
      is_node_type(json_member(gen_iter, "func"), "Name") &&
      json_string(json_member(json_member(gen_iter, "func"), "id")) == "range")
    {
      const jsont &args_n = json_member(gen_iter, "args");
      if(!args_n.is_array() || as_array(args_n).empty())
      {
        log.warning() << "range() in comprehension requires arguments"
                      << messaget::eom;
        return nil_exprt{};
      }
      std::vector<mp_integer> ints;
      bool ok = true;
      const bool single_arg = as_array(args_n).size() == 1;
      exprt single_bound;
      for(const auto &a : as_array(args_n))
      {
        exprt av = convert_expression(a);
        if(single_arg)
          single_bound = av;
        // Try constant. Accept integer_typet (--python-unbounded-ints)
        // as well as signedbv, else range() args are unrecognised
        // under that flag and the comprehension is silently dropped.
        if(
          av.is_constant() &&
          (av.type().id() == ID_signedbv || av.type().id() == ID_integer))
        {
          mp_integer v;
          if(!to_integer(to_constant_expr(av), v))
          {
            ints.push_back(v);
            continue;
          }
        }
        // Fall back to try_eval_double for tracked-symbol cases
        auto evd = try_eval_double(av);
        if(evd.has_value())
        {
          ints.push_back(mp_integer{static_cast<long long>(evd.value())});
          continue;
        }
        ok = false;
        break;
      }
      if(!ok || ints.size() < 1 || ints.size() > 3)
      {
        // PLR §6.3.3: `[f(x) for x in range(n)]` with a NON-constant int bound
        // cannot be unrolled, but its result LENGTH is the bound (clamped to
        // [0, capacity]: range(<0) is empty, range(>=cap) is modelled up to the
        // array capacity). Return a symbolic-length list with nondet data --
        // NOT nil, which would DROP the enclosing assignment and unsoundly
        // retain the target's stale value (e.g. `xs = sorted([5]); xs =
        // [i for i in range(len(xs))][1:5]` kept the old [5], hiding the
        // min()-of-empty ValueError). Element values are unknown (nondet); only
        // the length is tracked, which is what len()/slice/min-empty need.
        const bool bound_is_int = !single_bound.is_nil() &&
                                  (single_bound.type().id() == ID_signedbv ||
                                   single_bound.type().id() == ID_integer ||
                                   single_bound.type().id() == ID_unsignedbv ||
                                   single_bound.type() == python_int_type());
        // Only safe with exactly ONE generator. A filter (`if ...`) means fewer
        // elements survive, so the length is not exactly the bound -- handled
        // below by a NONDET length bounded by the range size (still lets
        // min()-of-empty fire). Multiple generators multiply lengths -> fall
        // through to conservative nil.
        const jsont &cur_ifs = json_member(gen, "ifs");
        const bool no_ifs = !cur_ifs.is_array() || as_array(cur_ifs).empty();
        const bool single_gen = as_array(generators).size() == 1;
        if(single_arg && bound_is_int && single_gen)
        {
          const source_locationt loc = get_location(expr);
          typet et = python_int_type();
          struct_typet lt = python_list_type(et);
          static unsigned rc_ctr = 0;
          irep_idt tid{
            qualify_name("__range_comp_" + std::to_string(rc_ctr++))};
          if(symbol_table.lookup(tid) == nullptr)
          {
            symbolt ts{tid, lt, "python"};
            ts.base_name = id2string(tid);
            ts.is_lvalue = true;
            ts.is_state_var = true;
            ts.is_static_lifetime = current_function.empty();
            symbol_table.add(ts);
          }
          symbol_exprt tmp = symbol_table.lookup_ref(tid).symbol_expr();
          // Nondet the whole list (havocs length AND data)...
          pending_checks.push_back(
            code_frontend_assignt{tmp, side_effect_expr_nondett{lt, loc}});
          exprt b64 = safe_typecast(single_bound, signedbv_typet{64});
          exprt zero = from_integer(0, signedbv_typet{64});
          exprt cap = from_integer(PYTHON_MAX_LIST_LENGTH, signedbv_typet{64});
          exprt clamped = if_exprt{
            binary_relation_exprt{b64, ID_lt, zero},
            zero,
            if_exprt{binary_relation_exprt{b64, ID_gt, cap}, cap, b64}};
          member_exprt len_m{tmp, "length", signedbv_typet{64}};
          if(no_ifs)
            // No filter: length is EXACTLY the range size.
            pending_checks.push_back(code_frontend_assignt{len_m, clamped});
          else
          {
            // Filter: 0 <= length <= range size (unknown how many survive).
            code_assumet a{and_exprt{
              binary_relation_exprt{len_m, ID_ge, zero},
              binary_relation_exprt{len_m, ID_le, clamped}}};
            a.add_source_location() = loc;
            pending_checks.push_back(std::move(a));
          }
          return std::move(tmp);
        }
        log.warning()
          << "range() in comprehension requires constant integer arguments"
          << messaget::eom;
        return nil_exprt{};
      }
      mp_integer start{0}, stop, step{1};
      if(ints.size() == 1)
        stop = ints[0];
      else
      {
        start = ints[0];
        stop = ints[1];
        if(ints.size() == 3)
          step = ints[2];
      }
      if(step == 0)
      {
        log.warning() << "range() step cannot be zero" << messaget::eom;
        return nil_exprt{};
      }
      // Iteration values take the Python int type so the bound
      // variable composes with other int literals/operations in the
      // element expression (under --python-unbounded-ints both are
      // integer_typet; in the default mode both are signedbv[64]).
      typet i64 = python_int_type();
      if(step > 0)
      {
        for(mp_integer i = start; i < stop; i += step)
          gi.const_values.push_back(from_integer(i, i64));
      }
      else
      {
        for(mp_integer i = start; i > stop; i += step)
          gi.const_values.push_back(from_integer(i, i64));
      }
    }
    else
    {
      // Generic fallback: evaluate the iterable expression and,
      // if it produces a list struct with a compile-time-constant
      // length, extract its data values. This covers iterables
      // that aren't a literal list, a tracked Name, or range() —
      // most importantly a function call returning a list
      // (`[g(x) for x in f()]`) and a subscript/attribute that
      // resolves to a list literal. Without this, such
      // comprehensions returned nil and the enclosing assignment
      // was silently dropped (the target Name became nondet).
      exprt iter_val = convert_expression(gen_iter);
      bool extracted = false;
      // When the iterable is a call to a function that returns a
      // constant list literal, convert_expression yields a
      // function-call side-effect rather than the literal struct.
      // Recover the literal from function_returned_literal (the
      // same single-return-literal cache used by dict/list
      // propagation in convert_assign).
      if(
        (iter_val.is_nil() || iter_val.id() == ID_side_effect) &&
        is_node_type(gen_iter, "Call") &&
        is_node_type(json_member(gen_iter, "func"), "Name"))
      {
        std::string callee =
          json_string(json_member(json_member(gen_iter, "func"), "id"));
        auto fl_it = function_returned_literal.find(callee);
        auto rc_it = function_return_count.find(callee);
        if(
          fl_it != function_returned_literal.end() &&
          rc_it != function_return_count.end() && rc_it->second == 1 &&
          is_python_list_type(fl_it->second.type()))
          iter_val = fl_it->second;
      }
      if(
        !iter_val.is_nil() && is_python_list_type(iter_val.type()) &&
        iter_val.id() == ID_struct && iter_val.operands().size() >= 2 &&
        iter_val.operands()[0].is_constant())
      {
        // Restrict to scalar element types. When the iterable's
        // elements are themselves containers (list/dict/tuple/
        // python_value), binding them to the iteration variable
        // and evaluating the element expression can exercise
        // container-valued code paths (e.g. `[1] + r` list
        // concatenation) whose nondet/exception modelling would
        // turn a previously-vacuous comprehension into a spurious
        // failure. The common useful case — a function returning
        // a list of scalars — is fully covered.
        const auto &iv_data =
          to_array_type(to_struct_type(iter_val.type()).components()[1].type());
        const typet &elem_t = iv_data.element_type();
        bool scalar_elem =
          elem_t.id() == ID_signedbv || elem_t.id() == ID_unsignedbv ||
          elem_t.id() == ID_floatbv || elem_t.id() == ID_bool ||
          is_python_string_type(elem_t);
        mp_integer len;
        if(
          scalar_elem &&
          !to_integer(to_constant_expr(iter_val.operands()[0]), len))
        {
          const exprt &data_arr = iter_val.operands()[1];
          for(mp_integer i = 0; i < len; ++i)
          {
            auto el = list_literal_element(data_arr, i.to_ulong());
            if(el.has_value())
              gi.const_values.push_back(std::move(*el));
          }
          extracted = true;
        }
      }
      if(!extracted)
      {
        log.warning()
          << "List comprehension iterable shape not supported (only literal "
             "list, name-of-tracked-list, range(), or a call/expression "
             "yielding a constant-length list)"
          << messaget::eom;
        return nil_exprt{};
      }
    }
    gens.push_back(std::move(gi));
  }

  if(gens.empty())
    return nil_exprt{};

  // Create symbols for all iteration variables
  for(auto &gi : gens)
  {
    std::string qname = qualify_name(gi.var_name);
    irep_idt sym_id{qname};
    if(symbol_table.lookup(sym_id) == nullptr)
    {
      symbolt sym{sym_id, python_int_type(), "python"};
      sym.base_name = gi.var_name;
      sym.is_lvalue = true;
      sym.is_state_var = true;
      symbol_table.add(sym);
    }
  }

  // Closure cell substrate (PLR §4.2.2), comprehension late-binding.
  // When the element is a closure (lambda) and no filters are present,
  // Python-3 semantics make every element closure share the loop
  // variable's cell, so all observe its FINAL value. Bind a unique
  // per-comprehension symbol to that final value and redirect the loop
  // variable to it while the element closures are converted, so they
  // reference the unique symbol (not the enclosing same-named binding,
  // which must not be clobbered). Gated to the closure element + no-ifs
  // case; anything else keeps the existing per-combination substitution.
  std::vector<std::string> comp_redirect_keys;
  {
    bool no_ifs = true;
    for(const auto &gen : as_array(generators))
    {
      const jsont &ifs = json_member(gen, "ifs");
      if(ifs.is_array() && !as_array(ifs).empty())
        no_ifs = false;
    }
    if(is_node_type(elt, "Lambda") && no_ifs)
    {
      static unsigned lcb_ctr = 0;
      for(auto &gi : gens)
      {
        if(gi.const_values.empty())
          continue; // only compile-time-enumerable iterables
        const exprt &final_val = gi.const_values.back();
        std::string un = gi.var_name + "$lc" + std::to_string(lcb_ctr++);
        irep_idt uid{qualify_name(un)};
        if(symbol_table.lookup(uid) == nullptr)
        {
          symbolt us{uid, final_val.type(), "python"};
          us.base_name = un;
          us.is_lvalue = true;
          us.is_state_var = true;
          us.is_static_lifetime = current_function.empty();
          symbol_table.add(us);
        }
        pending_checks.push_back(code_frontend_assignt{
          symbol_table.lookup_ref(uid).symbol_expr(), final_val});
        comprehension_var_redirect[gi.var_name] = uid;
        comp_redirect_keys.push_back(gi.var_name);
      }
    }
  }

  // Unroll all combinations
  // For single generator: iterate values
  // For nested: iterate cartesian product
  std::vector<std::vector<std::size_t>> combos;
  combos.push_back({});
  for(const auto &gi : gens)
  {
    std::vector<std::vector<std::size_t>> new_combos;
    // Use whichever source of values is populated.
    std::size_t n =
      gi.values.empty() ? gi.const_values.size() : gi.values.size();
    for(const auto &combo : combos)
    {
      for(std::size_t i = 0; i < n; i++)
      {
        auto new_combo = combo;
        new_combo.push_back(i);
        new_combos.push_back(std::move(new_combo));
      }
    }
    combos = std::move(new_combos);
  }

  // Evaluate elt for each combination
  exprt::operandst elements;
  // A filter clause that does not constant-evaluate cannot be decided
  // during unrolling; detect it and bail out to an exact loop lowering
  // (or a sound over-approximation) below. Snapshot pending_checks so
  // the abandoned partial unroll's side-effect checks are rolled back.
  bool symbolic_filter = false;
  const std::size_t pc_snapshot = pending_checks.size();
  for(const auto &combo : combos)
  {
    // Convert each iteration variable's value
    std::vector<std::pair<irep_idt, exprt>> bindings;
    for(std::size_t g = 0; g < gens.size(); g++)
    {
      exprt val;
      if(!gens[g].values.empty())
        val = convert_expression(*gens[g].values[combo[g]]);
      else
        val = gens[g].const_values[combo[g]];
      bindings.push_back({irep_idt{qualify_name(gens[g].var_name)}, val});
    }

    // Bind types and string-constant values for THIS combination BEFORE
    // converting the element/filters: the iteration symbols are created
    // above with a placeholder int type, so conversion-time folds and
    // coercions otherwise see wrongly-typed operands (e.g. a
    // `'s' in x` filter folded against an int-typed symbol to a
    // definite-wrong constant, emptying or overfilling the result —
    // caught by ground-truth probes).
    for(const auto &[sym_id, val] : bindings)
    {
      symbol_table.get_writeable_ref(sym_id).type = val.type();
      auto sv = extract_string_value(val);
      if(sv.has_value())
        string_constants[sym_id] = sv.value();
      else
        string_constants.erase(sym_id);
    }

    // Evaluate elt and substitute
    exprt elt_expr = convert_expression(elt);
    for(const auto &binding : bindings)
    {
      // Named locals rather than a structured binding: capturing a
      // structured binding in a lambda is a C++20 extension AppleClang
      // rejects under -Werror in this C++17 build.
      const irep_idt &b_sym = binding.first;
      const exprt &b_val = binding.second;
      std::function<void(exprt &)> subst = [&](exprt &e)
      {
        if(e.id() == ID_symbol && to_symbol_expr(e).get_identifier() == b_sym)
          e = b_val;
        else
          for(auto &op : e.operands())
            subst(op);
      };
      subst(elt_expr);
    }
    // Check if conditions (filters) for this combination
    bool passes_filter = true;
    for(std::size_t g = 0; g < gens.size() && passes_filter; g++)
    {
      const jsont &gen = *std::next(as_array(generators).begin(), g);
      const jsont &ifs = json_member(gen, "ifs");
      if(ifs.is_array())
      {
        for(const auto &cond_json : as_array(ifs))
        {
          exprt cond_expr = convert_expression(cond_json);
          for(const auto &binding : bindings)
          {
            // Named locals: capturing a structured binding is a C++20
            // extension (see the elt-substitution loop above).
            const irep_idt &b_sym = binding.first;
            const exprt &b_val = binding.second;
            std::function<void(exprt &)> subst2 = [&](exprt &e)
            {
              if(
                e.id() == ID_symbol &&
                to_symbol_expr(e).get_identifier() == b_sym)
                e = b_val;
              else
                for(auto &op : e.operands())
                  subst2(op);
            };
            subst2(cond_expr);
          }
          // Try constant evaluation: check if all operands are constant
          // and the result can be computed
          // Try constant evaluation after substitution
          // Recursively simplify the expression
          std::function<exprt(const exprt &)> try_eval =
            [&](const exprt &e) -> exprt
          {
            if(e.is_constant())
              return e;
            // Simplify binary ops with constant operands
            if(e.operands().size() == 2)
            {
              exprt l = try_eval(e.operands()[0]);
              exprt r = try_eval(e.operands()[1]);
              if(
                l.is_constant() && r.is_constant() &&
                l.type().id() == ID_signedbv)
              {
                mp_integer a, b;
                if(
                  !to_integer(to_constant_expr(l), a) &&
                  !to_integer(to_constant_expr(r), b))
                {
                  if(e.id() == ID_mod && b != 0)
                    return from_integer(a % b, l.type());
                  if(e.id() == ID_plus)
                    return from_integer(a + b, l.type());
                  if(e.id() == ID_minus)
                    return from_integer(a - b, l.type());
                  if(e.id() == ID_mult)
                    return from_integer(a * b, l.type());
                  if(e.id() == ID_bitxor)
                  {
                    long long av = a.to_long(), bv = b.to_long();
                    return from_integer(av ^ bv, l.type());
                  }
                  if(e.id() == ID_div && b != 0)
                    return from_integer(a / b, l.type());
                  if(e.id() == ID_equal)
                    return a == b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_notequal)
                    return a != b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_lt)
                    return a < b ? static_cast<exprt>(true_exprt{})
                                 : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_gt)
                    return a > b ? static_cast<exprt>(true_exprt{})
                                 : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_le)
                    return a <= b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_ge)
                    return a >= b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                }
              }
            }
            // if_exprt: evaluate condition
            if(e.id() == ID_if && e.operands().size() == 3)
            {
              exprt c = try_eval(e.operands()[0]);
              if(c.is_true())
                return try_eval(e.operands()[1]);
              if(c.is_false())
                return try_eval(e.operands()[2]);
            }
            // and/or
            if(e.id() == ID_and && e.operands().size() == 2)
            {
              exprt l = try_eval(e.operands()[0]);
              exprt r = try_eval(e.operands()[1]);
              if(l.is_false() || r.is_false())
                return false_exprt{};
              if(l.is_true() && r.is_true())
                return true_exprt{};
            }
            if(e.id() == ID_not && e.operands().size() == 1)
            {
              exprt o = try_eval(e.operands()[0]);
              if(o.is_true())
                return false_exprt{};
              if(o.is_false())
                return true_exprt{};
            }
            return e;
          };
          exprt simplified = try_eval(cond_expr);
          if(simplified.is_false())
            passes_filter = false;
          else if(!simplified.is_true())
          {
            // Symbolic filter: neither provably true nor false at
            // conversion time. The unroll previously INCLUDED such
            // elements unconditionally, producing definite-wrong list
            // contents in both directions (false failures and false
            // proofs of len()/content properties). Bail out to the
            // loop lowering below.
            symbolic_filter = true;
            passes_filter = false;
          }
        }
      }
    }
    if(symbolic_filter)
      break;
    if(!passes_filter)
      continue;
    // PLR §6.2.7: if the element expression is `str(<iter-var>)`,
    // fold it to a concrete python_string_literal at conversion
    // time. (Mirrors the dict-comp constant-folding for the same
    // reason: the runtime-emitted cprover_string_of_int_func is
    // a side effect that the element-equal comparison can't see
    // through.)
    auto try_fold_str_call =
      [&](
        const jsont &call_ast,
        const std::vector<std::pair<irep_idt, exprt>> &binds) -> exprt
    {
      if(!is_node_type(call_ast, "Call"))
        return nil_exprt{};
      const jsont &func = json_member(call_ast, "func");
      if(!is_node_type(func, "Name"))
        return nil_exprt{};
      std::string fn_name = json_string(json_member(func, "id"));
      const jsont &args_n = json_member(call_ast, "args");
      if(!args_n.is_array() || as_array(args_n).size() != 1)
        return nil_exprt{};
      const jsont &arg = *as_array(args_n).begin();
      if(!is_node_type(arg, "Name"))
        return nil_exprt{};
      std::string aname = json_string(json_member(arg, "id"));
      irep_idt qid{qualify_name(aname)};
      const exprt *bound = nullptr;
      for(const auto &[sid, val] : binds)
      {
        if(sid == qid)
        {
          bound = &val;
          break;
        }
      }
      if(bound == nullptr || !bound->is_constant())
        return nil_exprt{};
      mp_integer iv;
      if(
        bound->type().id() != ID_signedbv ||
        to_integer(to_constant_expr(*bound), iv))
        return nil_exprt{};
      if(fn_name == "str")
        return python_string_literal(integer2string(iv));
      return nil_exprt{};
    };
    if(exprt folded = try_fold_str_call(elt, bindings); folded.is_not_nil())
      elt_expr = std::move(folded);
    elements.push_back(elt_expr);
  }

  // Clear the per-combination string-constant bindings of the
  // iteration variables (they are conversion-scope only).
  for(const auto &gi : gens)
    string_constants.erase(irep_idt{qualify_name(gi.var_name)});

  // Clear the comprehension late-binding redirect now that all element
  // closures have been converted (they have baked in the unique symbol).
  for(const auto &k : comp_redirect_keys)
    comprehension_var_redirect.erase(k);

  if(symbolic_filter)
  {
    // Roll back checks emitted by the abandoned partial unroll.
    pending_checks.erase(
      pending_checks.begin() + pc_snapshot, pending_checks.end());
    const source_locationt loc = get_location(expr);
    // Exact route for the single-generator case: materialize the
    // iterable as a list value and lower to a real filtered loop
    // (emit_listcomp_loop evaluates the filter per iteration).
    if(gens.size() == 1)
    {
      const jsont &gen0 = *as_array(generators).begin();
      const jsont &gen0_iter = json_member(gen0, "iter");
      exprt iter_val = convert_expression(gen0_iter);
      if(
        (iter_val.is_nil() || !is_python_list_type(iter_val.type())) &&
        !gens[0].const_values.empty())
      {
        // range() with constant bounds: build the list struct from the
        // enumerated values (homogeneous ints).
        const typet et = gens[0].const_values.front().type();
        struct_typet lt = python_list_type(et);
        const typet stored_et =
          to_array_type(lt.components()[1].type()).element_type();
        const std::size_t cap = std::max<std::size_t>(
          PYTHON_MAX_LIST_LENGTH, gens[0].const_values.size());
        array_typet dt{stored_et, from_integer(cap, signedbv_typet{64})};
        {
          auto &comps = lt.components();
          if(comps.size() == 2)
            comps[1].type() = dt;
        }
        exprt::operandst data_elems;
        bool homogeneous = true;
        for(const auto &cv : gens[0].const_values)
        {
          if(cv.type() != stored_et)
          {
            homogeneous = false;
            break;
          }
          data_elems.push_back(cv);
        }
        if(homogeneous)
        {
          while(data_elems.size() < cap)
            data_elems.push_back(safe_zero(stored_et));
          iter_val = struct_exprt{
            {from_integer(
               static_cast<long long>(gens[0].const_values.size()),
               signedbv_typet{64}),
             build_list_data(std::move(data_elems), dt)},
            lt};
        }
      }
      if(!iter_val.is_nil() && is_python_list_type(iter_val.type()))
      {
        exprt r = emit_listcomp_loop(
          elt, gens[0].var_name, iter_val, json_member(gen0, "ifs"), loc);
        if(!r.is_nil())
          return r;
        pending_checks.erase(
          pending_checks.begin() + pc_snapshot, pending_checks.end());
      }
    }
    // Sound over-approximation for the remaining shapes (multiple
    // generators, or an iterable emit_listcomp_loop cannot take):
    // nondet data with 0 <= length <= unfiltered-combination count.
    // Imprecise but never definite-wrong, unlike the previous
    // include-unconditionally behaviour.
    typet et = python_int_type();
    struct_typet lt = python_list_type(et);
    static unsigned sf_ctr = 0;
    irep_idt tid{qualify_name("__symfilter_comp_" + std::to_string(sf_ctr++))};
    if(symbol_table.lookup(tid) == nullptr)
    {
      symbolt ts{tid, lt, "python"};
      ts.base_name = id2string(tid);
      ts.is_lvalue = true;
      ts.is_state_var = true;
      ts.is_static_lifetime = current_function.empty();
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(tid).symbol_expr();
    pending_checks.push_back(
      code_frontend_assignt{tmp, side_effect_expr_nondett{lt, loc}});
    member_exprt len_m{tmp, "length", signedbv_typet{64}};
    exprt zero = from_integer(0, signedbv_typet{64});
    exprt nmax = from_integer(
      static_cast<long long>(
        std::min<std::size_t>(combos.size(), PYTHON_MAX_LIST_LENGTH)),
      signedbv_typet{64});
    code_assumet a{and_exprt{
      binary_relation_exprt{len_m, ID_ge, zero},
      binary_relation_exprt{len_m, ID_le, nmax}}};
    a.add_source_location() = loc;
    pending_checks.push_back(std::move(a));
    return std::move(tmp);
  }

  if(elements.empty())
  {
    // PLR §6.2.4: an empty comprehension yields an empty list,
    // NOT nil. Returning nil_exprt caused the surrounding
    // assignment / equality check to be silently dropped at
    // conversion time, masking incorrect assertions like
    // [x for x in []] == [0, 1, 4, 9].
    typet elem_type = python_int_type();
    struct_typet list_type = python_list_type(elem_type);
    const auto &data_type = to_array_type(list_type.components()[1].type());
    exprt::operandst zeros;
    return struct_exprt{
      {from_integer(0, signedbv_typet{64}),
       build_list_data(std::move(zeros), data_type)},
      list_type};
  }

  // Build the result list
  typet elem_type = elements[0].type();
  struct_typet list_type = python_list_type(elem_type);
  const auto &data_type = to_array_type(list_type.components()[1].type());

  exprt::operandst data_elems;
  for(auto &e : elements)
  {
    e = coerce_element(e, elem_type);
    data_elems.push_back(e);
  }

  exprt data = build_list_data(std::move(data_elems), data_type);
  exprt length =
    from_integer(static_cast<long long>(elements.size()), signedbv_typet{64});

  return struct_exprt{{length, data}, list_type};
}

// PLR §6.2.7: dict comprehension '{k: v for x in xs}'. Shares the
// generator-unrolling logic with convert_list_comp; builds a
// python_dict struct (length, keys array, values array) at the end.
exprt python_convertert::convert_dict_comp(const jsont &expr)
{
  const jsont &key_expr_json = json_member(expr, "key");
  const jsont &val_expr_json = json_member(expr, "value");
  const jsont &generators = json_member(expr, "generators");

  if(!generators.is_array() || as_array(generators).empty())
    return nil_exprt{};

  // Collect generators: supported iterables are literal lists and
  // range() calls with constant-integer arguments. Anything else
  // falls back to a nondet dict over-approximation.
  struct gen_info
  {
    // Plain-Name target: one name. Tuple-of-Names target (PLR §6.2.6:
    // `{k: v for k, v in pairs}` destructures each element): one name
    // per position.
    std::vector<std::string> var_names;
    // Either a list of jsont pointers from a literal [ ... ] or a
    // list of pre-computed integer values from a range().
    std::vector<const jsont *> json_values;
    std::vector<mp_integer> int_values;
    bool is_range = false;
    bool tuple_target = false;
  };
  std::vector<gen_info> gens;

  // Helper: try to evaluate range(a[, b[, c]]) with constant ints.
  auto try_range = [&](const jsont &call, std::vector<mp_integer> &out) -> bool
  {
    if(!is_node_type(call, "Call"))
      return false;
    const jsont &func = json_member(call, "func");
    if(!is_node_type(func, "Name"))
      return false;
    if(json_string(json_member(func, "id")) != "range")
      return false;
    const jsont &args_n = json_member(call, "args");
    if(!args_n.is_array() || as_array(args_n).empty())
      return false;
    std::vector<mp_integer> ints;
    for(const auto &a : as_array(args_n))
    {
      exprt av = convert_expression(a);
      if(!av.is_constant() || av.type().id() != ID_signedbv)
        return false;
      mp_integer v;
      if(to_integer(to_constant_expr(av), v))
        return false;
      ints.push_back(v);
    }
    mp_integer start{0}, stop, step{1};
    if(ints.size() == 1)
      stop = ints[0];
    else if(ints.size() == 2)
    {
      start = ints[0];
      stop = ints[1];
    }
    else if(ints.size() == 3)
    {
      start = ints[0];
      stop = ints[1];
      step = ints[2];
    }
    else
      return false;
    if(step == 0)
      return false;
    out.clear();
    if(step > 0)
    {
      for(mp_integer i = start; i < stop; i += step)
        out.push_back(i);
    }
    else
    {
      for(mp_integer i = start; i > stop; i += step)
        out.push_back(i);
    }
    return true;
  };

  for(const auto &gen : as_array(generators))
  {
    const jsont &gen_iter = json_member(gen, "iter");
    gen_info gi;
    const jsont &target = json_member(gen, "target");
    if(is_node_type(target, "Name"))
      gi.var_names.push_back(json_string(json_member(target, "id")));
    else if(is_node_type(target, "Tuple"))
    {
      // PLR §6.2.6 / §7.2: a tuple target destructures each element.
      // Support the flat Tuple-of-Names form; anything else (nested
      // tuples, starred targets) falls back to the sound nondet dict.
      const jsont &t_elts = json_member(target, "elts");
      bool all_names = t_elts.is_array() && !as_array(t_elts).empty();
      if(t_elts.is_array())
        for(const auto &te : as_array(t_elts))
        {
          if(is_node_type(te, "Name"))
            gi.var_names.push_back(json_string(json_member(te, "id")));
          else
            all_names = false;
        }
      if(!all_names)
      {
        log_overapprox(
          "dict comprehension with unsupported target shape: "
          "using nondet dict");
        return side_effect_expr_nondett{
          python_dict_type(python_value_type(), python_value_type()),
          source_locationt{}};
      }
      gi.tuple_target = true;
    }
    else
    {
      log_overapprox(
        "dict comprehension with unsupported target shape: "
        "using nondet dict");
      return side_effect_expr_nondett{
        python_dict_type(python_value_type(), python_value_type()),
        source_locationt{}};
    }
    std::vector<mp_integer> r_values;
    if(is_node_type(gen_iter, "List"))
    {
      const jsont &elts = json_member(gen_iter, "elts");
      if(elts.is_array())
      {
        for(const auto &e : as_array(elts))
          gi.json_values.push_back(&e);
      }
    }
    else if(try_range(gen_iter, r_values))
    {
      if(gi.tuple_target)
      {
        // PLR §7.2: destructuring an int raises TypeError; don't
        // mis-bind — fall back soundly.
        log_overapprox(
          "dict comprehension tuple target over range(): "
          "using nondet dict");
        return side_effect_expr_nondett{
          python_dict_type(python_value_type(), python_value_type()),
          source_locationt{}};
      }
      gi.is_range = true;
      gi.int_values = std::move(r_values);
    }
    else
    {
      log_overapprox(
        "dict comprehension with non-literal iterable: using nondet dict");
      return side_effect_expr_nondett{
        python_dict_type(python_value_type(), python_value_type()),
        source_locationt{}};
    }
    gens.push_back(std::move(gi));
  }

  // Register iteration-variable symbols.
  for(auto &gi : gens)
  {
    for(const auto &vn : gi.var_names)
    {
      std::string qname = qualify_name(vn);
      irep_idt sym_id{qname};
      if(symbol_table.lookup(sym_id) == nullptr)
      {
        symbolt sym{sym_id, python_int_type(), "python"};
        sym.base_name = vn;
        sym.is_lvalue = true;
        sym.is_state_var = true;
        symbol_table.add(sym);
      }
    }
  }

  // Cartesian-product unroll.
  std::vector<std::vector<std::size_t>> combos{{}};
  for(const auto &gi : gens)
  {
    const std::size_t n =
      gi.is_range ? gi.int_values.size() : gi.json_values.size();
    std::vector<std::vector<std::size_t>> next;
    for(const auto &combo : combos)
      for(std::size_t i = 0; i < n; i++)
      {
        auto c = combo;
        c.push_back(i);
        next.push_back(std::move(c));
      }
    combos = std::move(next);
  }

  // Evaluate (key, value) pairs for each combination, applying ifs.
  std::vector<std::pair<exprt, exprt>> pairs;
  for(const auto &combo : combos)
  {
    std::vector<std::pair<irep_idt, exprt>> bindings;
    for(std::size_t g = 0; g < gens.size(); g++)
    {
      if(gens[g].is_range)
      {
        bindings.push_back(
          {irep_idt{qualify_name(gens[g].var_names.front())},
           from_integer(gens[g].int_values[combo[g]], python_int_type())});
      }
      else if(!gens[g].tuple_target)
      {
        bindings.push_back(
          {irep_idt{qualify_name(gens[g].var_names.front())},
           convert_expression(*gens[g].json_values[combo[g]])});
      }
      else
      {
        // Tuple target: the element must be a literal Tuple of the
        // same arity (PLR §7.2 raises ValueError on arity mismatch
        // and TypeError on non-iterables; those shapes fall back to
        // the sound nondet dict rather than mis-binding).
        const jsont &ev = *gens[g].json_values[combo[g]];
        const jsont &ev_elts = json_member(ev, "elts");
        if(
          !is_node_type(ev, "Tuple") || !ev_elts.is_array() ||
          as_array(ev_elts).size() != gens[g].var_names.size())
        {
          log_overapprox(
            "dict comprehension tuple target over a non-tuple or "
            "arity-mismatched element: using nondet dict");
          return side_effect_expr_nondett{
            python_dict_type(python_value_type(), python_value_type()),
            source_locationt{}};
        }
        std::size_t vi = 0;
        for(const auto &ee : as_array(ev_elts))
          bindings.push_back(
            {irep_idt{qualify_name(gens[g].var_names[vi++])},
             convert_expression(ee)});
      }
    }

    std::function<void(exprt &)> subst = [&](exprt &e)
    {
      for(const auto &[sym_id, val] : bindings)
        if(e.id() == ID_symbol && to_symbol_expr(e).get_identifier() == sym_id)
        {
          e = val;
          return;
        }
      for(auto &op : e.operands())
        subst(op);
    };

    // Check filter 'if' clauses.
    bool passes = true;
    for(std::size_t g = 0; g < gens.size() && passes; g++)
    {
      const jsont &gen = *std::next(as_array(generators).begin(), g);
      const jsont &ifs = json_member(gen, "ifs");
      if(ifs.is_array())
      {
        for(const auto &cond_json : as_array(ifs))
        {
          exprt cond = convert_expression(cond_json);
          subst(cond);
          simplify(cond, namespacet{symbol_table});
          // Constant-fold obvious cases.
          if(cond.is_false())
          {
            passes = false;
            break;
          }
          if(!cond.is_true())
          {
            // Symbolic filter: not decidable during the unroll.
            // Treating it as true produced definite-wrong dict
            // contents (the same disease the list-comp unroll had);
            // over-approximate soundly instead.
            log_overapprox(
              "dict comprehension with non-constant filter: "
              "using nondet dict");
            return side_effect_expr_nondett{
              python_dict_type(python_value_type(), python_value_type()),
              source_locationt{}};
          }
        }
      }
    }
    if(!passes)
      continue;

    exprt k = convert_expression(key_expr_json);
    exprt v = convert_expression(val_expr_json);
    subst(k);
    subst(v);
    // PLR §3.2: a dict-comprehension key must be hashable; a list/dict/set key
    // raises TypeError ('unhashable type').
    if(!k.is_nil() && is_unhashable_type(k.type()))
      emit_conditional_exception(true_exprt{}, "TypeError");
    // PLR §6.2.7: when a dict-comp key/value is a function call
    // whose only non-constant argument was the iteration variable,
    // the post-substitution expression is a side-effect (e.g.
    // {__string_len, __string_ptr} for str(i)) that no longer
    // refers to the constant. To recover the constant fold, when
    // the key was the AST `str(<iter-var>)` we re-convert it with
    // the iteration variable replaced by its concrete constant.
    auto try_constant_fold_call =
      [&](
        const jsont &call_ast,
        const std::vector<std::pair<irep_idt, exprt>> &binds) -> exprt
    {
      if(!is_node_type(call_ast, "Call"))
        return nil_exprt{};
      const jsont &func = json_member(call_ast, "func");
      if(!is_node_type(func, "Name"))
        return nil_exprt{};
      std::string fn_name = json_string(json_member(func, "id"));
      const jsont &args_n = json_member(call_ast, "args");
      if(!args_n.is_array() || as_array(args_n).size() != 1)
        return nil_exprt{};
      const jsont &arg = *as_array(args_n).begin();
      // Only support an argument that is the iteration variable
      // directly. (Compound expressions like str(i+1) would
      // require recursive AST evaluation.)
      if(!is_node_type(arg, "Name"))
        return nil_exprt{};
      std::string aname = json_string(json_member(arg, "id"));
      irep_idt qid{qualify_name(aname)};
      const exprt *bound = nullptr;
      for(const auto &[sid, val] : binds)
      {
        if(sid == qid)
        {
          bound = &val;
          break;
        }
      }
      if(bound == nullptr || !bound->is_constant())
        return nil_exprt{};
      mp_integer iv;
      if(
        bound->type().id() != ID_signedbv ||
        to_integer(to_constant_expr(*bound), iv))
        return nil_exprt{};
      // Fold known builtins.
      if(fn_name == "str")
        return python_string_literal(integer2string(iv));
      return nil_exprt{};
    };
    if(exprt folded_k = try_constant_fold_call(key_expr_json, bindings);
       folded_k.is_not_nil())
      k = std::move(folded_k);
    if(exprt folded_v = try_constant_fold_call(val_expr_json, bindings);
       folded_v.is_not_nil())
      v = std::move(folded_v);
    pairs.emplace_back(std::move(k), std::move(v));
  }

  // Choose key/value types from the first pair (fall back to generic).
  typet key_type =
    pairs.empty() ? python_string_type() : pairs.front().first.type();
  typet val_type =
    pairs.empty() ? python_int_type() : pairs.front().second.type();
  struct_typet dict_type = python_dict_type(key_type, val_type);
  const auto &keys_arr_type = to_array_type(dict_type.components()[1].type());
  const auto &vals_arr_type = to_array_type(dict_type.components()[2].type());

  exprt::operandst key_elems, val_elems;
  for(auto &p : pairs)
  {
    p.first = coerce_element(p.first, key_type);
    p.second = coerce_element(p.second, val_type);
    key_elems.push_back(p.first);
    val_elems.push_back(p.second);
  }

  exprt length =
    from_integer(static_cast<long long>(pairs.size()), signedbv_typet{64});

  return struct_exprt{
    {length,
     build_list_data(std::move(key_elems), keys_arr_type),
     build_list_data(std::move(val_elems), vals_arr_type)},
    dict_type};
}
