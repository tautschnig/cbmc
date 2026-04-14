/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "arrays.h"

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/json.h>
#include <util/message.h>
#include <util/replace_expr.h>
#include <util/replace_symbol.h>
#include <util/std_expr.h>

#include <solvers/prop/literal_expr.h>
#include <solvers/prop/prop.h>

#ifdef DEBUG
#  include <util/format_expr.h>

#  include <iostream>
#endif

#include <unordered_set>

arrayst::arrayst(
  const namespacet &_ns,
  propt &_prop,
  message_handlert &_message_handler,
  bool _get_array_constraints)
  : map_theoryt(_ns, _prop, _message_handler, _get_array_constraints),
    message_handler(_message_handler)
{
}

void map_theoryt::record_array_index(const index_exprt &index)
{
  // we are not allowed to put the index directly in the
  //   entry for the root of the equivalence class
  //   because this map is accessed during building the error trace
  std::size_t number=arrays.number(index.array());
  if(index_map[number].insert(index.index()).second)
    update_indices.insert(number);
}

literalt arrayst::record_array_equality(
  const equal_exprt &equality)
{
  const exprt &op0=equality.op0();
  const exprt &op1=equality.op1();

  DATA_INVARIANT_WITH_DIAGNOSTICS(
    op0.type() == op1.type(),
    "record_array_equality got equality without matching types",
    irep_pretty_diagnosticst{equality});

  DATA_INVARIANT(
    op0.type().id() == ID_array,
    "record_array_equality parameter should be array-typed");

  array_equalities.push_back(array_equalityt());

  array_equalities.back().f1=op0;
  array_equalities.back().f2=op1;
  array_equalities.back().l=SUB::equality(op0, op1);

  arrays.make_union(op0, op1);
  collect_arrays(op0);
  collect_arrays(op1);

  // WEG: equality edge
  weg.add_equality(weg.number(op0), weg.number(op1));

  return array_equalities.back().l;
}

void arrayst::record_array_let_binding(
  const symbol_exprt &symbol,
  const exprt &value)
{
  DATA_INVARIANT(
    symbol.type().id() == ID_array,
    "record_array_let_binding parameter should be array-typed");

  const equal_exprt eq{symbol, value};
  const literalt eq_lit = record_array_equality(eq);
  array_equalities.back().asserted_true = true;
  asserted_true_literals.insert(eq_lit.get());
  prop.l_set_to_true(eq_lit);
}

void map_theoryt::collect_indices()
{
  for(std::size_t i=0; i<arrays.size(); i++)
  {
    collect_indices(arrays[i]);
  }
}

void map_theoryt::collect_indices(const exprt &expr)
{
  if(expr.id()!=ID_index)
  {
    if(expr.id() == ID_array_comprehension)
      array_comprehension_args.insert(
        to_array_comprehension_expr(expr).arg().get_identifier());

    for(const auto &op : expr.operands())
      collect_indices(op);
  }
  else
  {
    const index_exprt &e = to_index_expr(expr);

    if(
      e.index().id() == ID_symbol &&
      array_comprehension_args.count(
        to_symbol_expr(e.index()).get_identifier()) != 0)
    {
      return;
    }

    collect_indices(e.index()); // necessary?

    const typet &array_op_type = e.array().type();

    if(array_op_type.id()==ID_array)
    {
      const array_typet &array_type=
        to_array_type(array_op_type);

      if(is_unbounded_array(array_type))
      {
        record_array_index(e);
      }
    }
  }
}

void map_theoryt::collect_arrays(const exprt &a)
{
  const array_typet &array_type = to_array_type(a.type());

  if(a.id()==ID_with)
  {
    const with_exprt &with_expr=to_with_expr(a);

    DATA_INVARIANT_WITH_DIAGNOSTICS(
      array_type == with_expr.old().type(),
      "collect_arrays got 'with' without matching types",
      irep_pretty_diagnosticst{a});

    arrays.make_union(a, with_expr.old());
    collect_arrays(with_expr.old());

    // WEG: a = store(old, where, value) — store edge
    const std::size_t a_weg = weg.number(a);
    const std::size_t old_weg = weg.number(with_expr.old());
    weg.add_store(old_weg, a_weg, with_expr.where());

    // make sure this shows as an application
    index_exprt index_expr(with_expr.old(), with_expr.where());
    // record_array_index(index_expr); // Yices2 optimization: skip when element theory is stably infinite
  }
  else if(a.id()==ID_update)
  {
    const update_exprt &update_expr=to_update_expr(a);

    DATA_INVARIANT_WITH_DIAGNOSTICS(
      array_type == update_expr.old().type(),
      "collect_arrays got 'update' without matching types",
      irep_pretty_diagnosticst{a});

    arrays.make_union(a, update_expr.old());
    collect_arrays(update_expr.old());

#if 0
    // make sure this shows as an application
    index_exprt index_expr(update_expr.old(), update_expr.index());
    record_array_index(index_expr);
#endif
  }
  else if(a.id()==ID_if)
  {
    const if_exprt &if_expr=to_if_expr(a);

    DATA_INVARIANT_WITH_DIAGNOSTICS(
      array_type == if_expr.true_case().type(),
      "collect_arrays got if without matching types",
      irep_pretty_diagnosticst{a});

    DATA_INVARIANT_WITH_DIAGNOSTICS(
      array_type == if_expr.false_case().type(),
      "collect_arrays got if without matching types",
      irep_pretty_diagnosticst{a});

    arrays.make_union(a, if_expr.true_case());
    arrays.make_union(a, if_expr.false_case());
    collect_arrays(if_expr.true_case());
    collect_arrays(if_expr.false_case());

    // WEG: if(c, t, f) — equality edges (no store index)
    const std::size_t a_weg = weg.number(a);
    const std::size_t t_weg = weg.number(if_expr.true_case());
    const std::size_t f_weg = weg.number(if_expr.false_case());
    weg.add_equality(a_weg, t_weg);
    weg.add_equality(a_weg, f_weg);
  }
  else if(a.id()==ID_symbol)
  {
  }
  else if(a.id()==ID_nondet_symbol)
  {
  }
  else if(a.id()==ID_member)
  {
  }
  else if(a.is_constant() || a.id() == ID_array || a.id() == ID_string_constant)
  {
  }
  else if(a.id()==ID_array_of)
  {
  }
  else if(a.id()==ID_byte_update_little_endian ||
          a.id()==ID_byte_update_big_endian)
  {
    DATA_INVARIANT(
      false,
      "byte_update should be removed before collect_arrays");
  }
  else if(a.id()==ID_typecast)
  {
    const auto &typecast_op = to_typecast_expr(a).op();

    // cast between array types?
    DATA_INVARIANT(
      typecast_op.type().id() == ID_array,
      "unexpected array type cast from " + typecast_op.type().id_string());

    // Only unify when element types match; casts between different
    // element sizes (e.g., SIMD reinterpretation) are handled at the
    // bitvector level.
    if(
      to_array_type(a.type()).element_type() ==
      to_array_type(typecast_op.type()).element_type())
    {
      arrays.make_union(a, typecast_op);

      // WEG: typecast is an equality edge
      weg.add_equality(weg.number(a), weg.number(typecast_op));
    }
    collect_arrays(typecast_op);
  }
  else if(a.id()==ID_index)
  {
    // nested unbounded arrays
    const auto &array_op = to_index_expr(a).array();
    arrays.make_union(a, array_op);
    collect_arrays(array_op);
  }
  else if(a.id() == ID_array_comprehension)
  {
  }
  else if(auto let_expr = expr_try_dynamic_cast<let_exprt>(a))
  {
    arrays.make_union(a, let_expr->where());
    collect_arrays(let_expr->where());
  }
  else
  {
    DATA_INVARIANT(
      false,
      "unexpected array expression (collect_arrays): '" + a.id_string() + "'");
  }
}

/// adds array constraints (refine=true...lazily for the refinement loop)
void map_theoryt::add_array_constraint(const lazy_constraintt &lazy, bool refine)
{
  if(lazy_arrays && refine)
  {
    // lazily add the constraint
    if(incremental_cache)
    {
      if(expr_map.find(lazy.lazy) == expr_map.end())
      {
        lazy_array_constraints.push_back(lazy);
        expr_map[lazy.lazy] = true;
      }
    }
    else
    {
      lazy_array_constraints.push_back(lazy);
    }
  }
  else
  {
    // add the constraint eagerly
    prop.l_set_to_true(convert(lazy.lazy));
  }
}

void arrayst::add_array_constraints()
{
  collect_indices();
  // at this point all indices should in the index set

  // Extensionality: for each non-trivial array equality l <-> (f1 = f2),
  // add Skolem diff index (one per equivalence class) and assert
  // f1[diff]=f2[diff] -> l.
  //
  // When lazy_arrays is set (--refine-arrays), skip eager extensionality;
  // it will be added on demand in the refinement loop.
  if(!lazy_arrays)
  {
    std::map<std::size_t, symbol_exprt> class_diff_index;

    for(auto &equality : array_equalities)
    {
      if(equality.l == const_literal(true))
        continue;

      if(
        equality.asserted_true ||
        asserted_true_literals.count(equality.l.get()))
      {
        continue;
      }

      const array_typet &array_type = to_array_type(equality.f1.type());

      typet index_type = array_type.index_type();
      if(
        can_cast_type<bitvector_typet>(index_type) &&
        to_bitvector_type(index_type).get_width() == 0)
      {
        index_type = array_type.size().type();
      }

      const typet &element_type = array_type.element_type();
      const std::size_t root = arrays.find_number(equality.f1);

      // WEG-based extensionality (weakeq-ext): use store indices on
      // the WEG path instead of a global diff index. If the arrays
      // agree at all store indices on the path, they must be equal.
      const std::size_t weg_f1 = weg.number(equality.f1);
      const std::size_t weg_f2 = weg.number(equality.f2);
      const auto path_indices = weg.path_store_indices(weg_f1, weg_f2);

      if(!path_indices.empty())
      {
        // weakeq-ext: /\(f1[i]=f2[i] for i in Stores(path)) -> l
        bvt neg_lits;
        for(const auto &idx : path_indices)
        {
          const index_exprt e1{equality.f1, idx, element_type};
          const index_exprt e2{equality.f2, idx, element_type};
          neg_lits.push_back(!convert(equal_exprt{e1, e2}));
        }
        neg_lits.push_back(equality.l);
        prop.lcnf(neg_lits);
      }
      else
      {
        // No store indices on path (equality edges only) — the arrays
        // should be equal. But we still need a diff index as fallback
        // for cases where the WEG path doesn't capture all constraints.
        auto [it, inserted] = class_diff_index.emplace(
          root,
          symbol_exprt{
            "array_theory::diff#" + std::to_string(extensionality_counter),
            index_type});
        if(inserted)
        {
          diff_indices.insert(it->second.get_identifier());
          index_map[root].insert(it->second);
          extensionality_counter++;
        }

        const symbol_exprt &diff_index = it->second;
        const index_exprt elem1{equality.f1, diff_index, element_type};
        const index_exprt elem2{equality.f2, diff_index, element_type};
        const literalt elem_eq_lit = convert(equal_exprt{elem1, elem2});
        prop.lcnf(!elem_eq_lit, equality.l);
      }
    }
  } // if(!lazy_arrays)

  // reduce initial index map
  update_index_map(true);

  // Diagnostic: report index set sizes and array counts per class
  {
    std::map<std::size_t, std::size_t> class_array_count;
    std::map<std::size_t, std::size_t> class_with_count;
    std::map<std::size_t, std::size_t> class_symbol_count;
    for(std::size_t i = 0; i < arrays.size(); i++)
    {
      std::size_t root = arrays.find_number(i);
      class_array_count[root]++;
      if(arrays[i].id() == ID_with)
        class_with_count[root]++;
      if(arrays[i].id() == ID_symbol || arrays[i].id() == ID_nondet_symbol)
        class_symbol_count[root]++;
    }
    for(const auto &[root, count] : class_array_count)
    {
      if(count > 1)
      {
        log.statistics() << "Array class " << root << ": " << count
                         << " arrays (" << class_symbol_count[root]
                         << " symbols, " << class_with_count[root] << " with), "
                         << index_map[root].size() << " indices, "
                         << (index_map[root].size() *
                             (index_map[root].size() - 1) / 2) *
                              class_symbol_count[root]
                         << " potential Ackermann" << messaget::eom;
      }
    }
  }

  if(use_read_over_weakeq)
  {
    // WEG-based: read-over-weakeq replaces Ackermann and the "else"
    // part of element-wise constraints. But we still need:
    // 1. The store axiom (idx): store(a, i, v)[i] = v
    // 2. Equality constraints for array equality literals
    // 3. array_of, array_constant, comprehension constraints

    // Generate store axiom (idx) for each with-expression
    for(std::size_t i = 0; i < arrays.size(); i++)
    {
      const exprt &a = arrays[i];
      if(a.id() == ID_with)
      {
        const with_exprt &with_expr = to_with_expr(a);
        const typet &element_type = to_array_type(a.type()).element_type();
        // store(old, where, value)[where] = value
        index_exprt index_expr{a, with_expr.where(), element_type};
        prop.l_set_to_true(
          convert(equal_exprt{index_expr, with_expr.new_value()}));
      }
      else if(a.id() == ID_array_of)
      {
        // array_of(v)[i] = v for all indices
        const index_sett &idx_set = index_map[arrays.find_number(i)];
        for(const auto &index : idx_set)
        {
          const typet &element_type = to_array_type(a.type()).element_type();
          index_exprt index_expr{a, index, element_type};
          prop.l_set_to_true(
            convert(equal_exprt{index_expr, to_array_of_expr(a).what()}));
        }
      }
      else if(a.id() == ID_array_comprehension)
      {
        const auto &comp = to_array_comprehension_expr(a);
        const index_sett &idx_set = index_map[arrays.find_number(i)];
        for(const auto &index : idx_set)
        {
          index_exprt index_expr{a, index};
          exprt body = comp.body();
          replace_expr(comp.arg(), index, body);
          prop.l_set_to_true(convert(equal_exprt{index_expr, body}));
        }
      }
      else if(a.id() == ID_if)
      {
        const if_exprt &if_expr = to_if_expr(a);
        const literalt cond_lit = convert(if_expr.cond());
        const index_sett &idx_set = index_map[arrays.find_number(i)];
        const typet &element_type = to_array_type(a.type()).element_type();
        for(const auto &index : idx_set)
        {
          index_exprt e_if{a, index, element_type};
          index_exprt e_true{if_expr.true_case(), index, element_type};
          index_exprt e_false{if_expr.false_case(), index, element_type};
          prop.lcnf(!cond_lit, convert(equal_exprt{e_if, e_true}));
          prop.lcnf(cond_lit, convert(equal_exprt{e_if, e_false}));
        }
      }
    }

    for(const auto &equality : array_equalities)
    {
      add_array_constraints_equality(
        index_map[arrays.find_number(equality.f1)], equality);
    }
    add_array_read_over_weakeq_constraints();
    return;
  }

  // add constraints for if, with, array_of, lambda
  std::set<std::size_t> roots_to_process, updated_roots;
  for(std::size_t i=0; i<arrays.size(); i++)
    roots_to_process.insert(arrays.find_number(i));

  while(!roots_to_process.empty())
  {
    for(std::size_t i = 0; i < arrays.size(); i++)
    {
      if(roots_to_process.count(arrays.find_number(i)) == 0)
        continue;

      // take a copy as arrays may get modified by add_array_constraints
      // in case of nested unbounded arrays
      exprt a = arrays[i];

      add_array_constraints(index_map[arrays.find_number(i)], a);

      // we have to update before it gets used in the next add_* call
      for(const std::size_t u : update_indices)
        updated_roots.insert(arrays.find_number(u));
      update_index_map(false);
    }

    roots_to_process = std::move(updated_roots);
    updated_roots.clear();
  }

  // add constraints for equalities
  for(const auto &equality : array_equalities)
  {
    add_array_constraints_equality(
      index_map[arrays.find_number(equality.f1)], equality);
  }

  // Use Ackermann with WEG-based skip.
  add_array_Ackermann_constraints();

  // Alternative: read-over-weakeq replaces both element-wise and
  // Ackermann constraints. Currently unused — requires removing the
  // element-wise constraints above to avoid redundancy.
  // add_array_read_over_weakeq_constraints();
}

/// Read-over-weakeq: for each pair of index expressions a[i] and b[j]
/// where a ≈_i b in the WEG, generate i=j → a[i]=b[j].
/// This replaces the quadratic Ackermann constraints with targeted
/// constraints based on weak equivalence paths.
void arrayst::add_array_read_over_weakeq_constraints()
{
  // Read-over-weakeq (Lemma 1): for each pair of select terms a[i], b[j]
  // where a ≈_i b (weakly equivalent modulo i) in the WEG, generate
  // i=j → a[i]=b[j]. For same-array pairs, generate standard Ackermann.
  //
  // Uses the forest-based get_rep_mod for correct modulo-i checks.

  struct select_termt
  {
    std::size_t weg_node;
    exprt index;
  };
  std::map<std::size_t, std::vector<select_termt>> class_selects;

  for(std::size_t i = 0; i < arrays.size(); i++)
  {
    // Use per-array index entries (pre-merge), not the merged root set.
    // This ensures we only generate read-over-weakeq for select terms
    // that actually appear in the formula for this specific array.
    const auto it = index_map.find(i);
    if(it == index_map.end() || it->second.empty())
      continue;

    const std::size_t root = arrays.find_number(i);
    const std::size_t weg_node = weg.number(arrays[i]);
    for(const auto &index : it->second)
      class_selects[root].push_back({weg_node, index});
  }

  for(auto &[root, selects] : class_selects)
  {
    // Deduplicate
    std::sort(
      selects.begin(), selects.end(), [](const auto &a, const auto &b) {
        return a.weg_node != b.weg_node ? a.weg_node < b.weg_node
                                        : a.index < b.index;
      });
    selects.erase(
      std::unique(
        selects.begin(),
        selects.end(),
        [](const auto &a, const auto &b) {
          return a.weg_node == b.weg_node && a.index == b.index;
        }),
      selects.end());

    for(std::size_t s1 = 0; s1 < selects.size(); s1++)
    {
      for(std::size_t s2 = s1 + 1; s2 < selects.size(); s2++)
      {
        const auto &a = selects[s1];
        const auto &b = selects[s2];

        if(a.index.is_constant() && b.index.is_constant() &&
           a.index != b.index)
          continue;

        // Same WEG node: Ackermann (functional consistency)
        // Different WEG nodes: read-over-weakeq (if a ≈_i b)
        if(a.weg_node != b.weg_node &&
           !weg.weakly_equivalent_mod(a.weg_node, b.weg_node, a.index))
          continue;

        const equal_exprt idx_eq{
          a.index,
          typecast_exprt::conditional_cast(b.index, a.index.type())};
        const literalt idx_eq_lit = convert(idx_eq);
        if(idx_eq_lit == const_literal(false))
          continue;

        const typet &elem_type =
          to_array_type(weg[a.weg_node].type()).element_type();
        const equal_exprt val_eq{
          index_exprt{weg[a.weg_node], a.index, elem_type},
          index_exprt{weg[b.weg_node], b.index, elem_type}};

        prop.lcnf(!idx_eq_lit, convert(val_eq));
        array_constraint_count[constraint_typet::ARRAY_ACKERMANN]++;
      }
    }
  }
}

void map_theoryt::add_array_Ackermann_constraints()
{
  // this is quadratic!

#ifdef DEBUG
  std::cout << "arrays.size(): " << arrays.size() << '\n';
#endif

  // Build set of "derived symbols": symbols that are transitively
  // equated to a derived array (with, if, etc.) via asserted-true
  // equalities. A symbol is derived if it equals a derived expression
  // OR another derived symbol. Computed as a fixed point.
  std::unordered_set<std::size_t> derived_symbol_indices;
  {
    auto is_derived_expr = [](const exprt &e)
    {
      return e.id() == ID_with || e.id() == ID_update || e.id() == ID_if ||
             e.id() == ID_array_of || e.id() == ID_array ||
             e.id() == ID_array_comprehension || e.id() == ID_typecast ||
             e.id() == ID_string_constant || e.is_constant() ||
             expr_try_dynamic_cast<let_exprt>(e) != nullptr;
    };
    auto is_symbol = [](const exprt &e)
    { return e.id() == ID_symbol || e.id() == ID_nondet_symbol; };

    // Collect asserted equalities between symbols and between
    // symbols and derived expressions.
    struct sym_eqt
    {
      std::size_t sym_idx;
      bool other_is_derived_expr;
      std::size_t other_sym_idx; // only valid if !other_is_derived_expr
    };
    std::vector<sym_eqt> sym_eqs;

    for(const auto &eq : array_equalities)
    {
      if(
        eq.l != const_literal(true) && !eq.asserted_true &&
        !asserted_true_literals.count(eq.l.get()))
      {
        continue;
      }

      // symbol = derived_expr
      if(is_symbol(eq.f1) && is_derived_expr(eq.f2))
        derived_symbol_indices.insert(arrays.number(eq.f1));
      else if(is_symbol(eq.f2) && is_derived_expr(eq.f1))
        derived_symbol_indices.insert(arrays.number(eq.f2));
      // symbol = symbol (for transitive closure)
      else if(is_symbol(eq.f1) && is_symbol(eq.f2))
      {
        sym_eqs.push_back({arrays.number(eq.f1), false, arrays.number(eq.f2)});
        sym_eqs.push_back({arrays.number(eq.f2), false, arrays.number(eq.f1)});
      }
    }

    // Fixed-point: propagate derived status through symbol=symbol edges
    bool changed = true;
    while(changed)
    {
      changed = false;
      for(const auto &se : sym_eqs)
      {
        if(
          !derived_symbol_indices.count(se.sym_idx) &&
          derived_symbol_indices.count(se.other_sym_idx))
        {
          derived_symbol_indices.insert(se.sym_idx);
          changed = true;
        }
      }
    }
  }

  // iterate over arrays
  for(std::size_t i=0; i<arrays.size(); i++)
  {
    // Skip arrays that are derived from other arrays via with, if, etc.
    const exprt &arr = arrays[i];
    if(
      arr.id() == ID_with || arr.id() == ID_update || arr.id() == ID_if ||
      arr.id() == ID_array_of || arr.id() == ID_array ||
      arr.id() == ID_array_comprehension || arr.id() == ID_typecast ||
      arr.id() == ID_string_constant || arr.is_constant())
    {
      continue;
    }
    if(expr_try_dynamic_cast<let_exprt>(arr))
      continue;

    // Also skip symbols that are defined as equal to a derived array
    // via an asserted equality (e.g., a_481 = store(a_480, i2, e2)).
    if(derived_symbol_indices.count(i))
      continue;

    const index_sett &index_set=index_map[arrays.find_number(i)];

#ifdef DEBUG
    std::cout << "index_set.size(): " << index_set.size() << '\n';
#endif

    // iterate over indices, 2x!
    for(index_sett::const_iterator
        i1=index_set.begin();
        i1!=index_set.end();
        i1++)
      for(index_sett::const_iterator
          i2=i1;
          i2!=index_set.end();
          i2++)
        if(i1!=i2)
        {
          if(i1->is_constant() && i2->is_constant())
            continue;

          // Skip Ackermann constraints between two extensionality diff
          // indices. Each diff index is a fresh Skolem symbol; constraints
          // between two such symbols are redundant.
          const bool i1_is_diff =
            i1->id() == ID_symbol &&
            diff_indices.count(to_symbol_expr(*i1).get_identifier());
          const bool i2_is_diff =
            i2->id() == ID_symbol &&
            diff_indices.count(to_symbol_expr(*i2).get_identifier());
          if(i1_is_diff && i2_is_diff)
            continue;

          // index equality
          const equal_exprt indices_equal(
            *i1, typecast_exprt::conditional_cast(*i2, i1->type()));

          literalt indices_equal_lit=convert(indices_equal);

          if(indices_equal_lit!=const_literal(false))
          {
            const typet &subtype =
              to_array_type(arrays[i].type()).element_type();
            index_exprt index_expr1(arrays[i], *i1, subtype);

            index_exprt index_expr2=index_expr1;
            index_expr2.index()=*i2;

            equal_exprt values_equal(index_expr1, index_expr2);

            // add constraint
            lazy_constraintt lazy(lazy_typet::ARRAY_ACKERMANN,
              implies_exprt(literal_exprt(indices_equal_lit), values_equal));
            add_array_constraint(lazy, true); // added lazily
            array_constraint_count[constraint_typet::ARRAY_ACKERMANN]++;

#if 0 // old code for adding, not significantly faster
            prop.lcnf(!indices_equal_lit, convert(values_equal));
#endif
          }
        }
  }
}

/// merge the indices into the root
void map_theoryt::update_index_map(std::size_t i)
{
  if(arrays.is_root_number(i))
    return;

  std::size_t root_number=arrays.find_number(i);
  INVARIANT(root_number!=i, "is_root_number incorrect?");

  index_sett &root_index_set=index_map[root_number];
  index_sett &index_set=index_map[i];

  root_index_set.insert(index_set.begin(), index_set.end());
}

void map_theoryt::update_index_map(bool update_all)
{
  // iterate over non-roots
  // possible reasons why update is needed:
  //  -- there are new equivalence classes in arrays
  //  -- there are new indices for arrays that are not the root
  //     of an equivalence class
  //     (and we cannot do that in record_array_index())
  //  -- equivalence classes have been merged
  if(update_all)
  {
    for(std::size_t i=0; i<arrays.size(); i++)
      update_index_map(i);
  }
  else
  {
    for(const auto &index : update_indices)
      update_index_map(index);

    update_indices.clear();
  }

#ifdef DEBUG
  // print index sets
  for(const auto &index_entry : index_map)
    for(const auto &index : index_entry.second)
      std::cout << "Index set (" << index_entry.first << " = "
                << arrays.find_number(index_entry.first) << " = "
                << format(arrays[arrays.find_number(index_entry.first)])
                << "): " << format(index) << '\n';
  std::cout << "-----\n";
#endif
}

void map_theoryt::add_array_constraints_equality(
  const index_sett &index_set,
  const array_equalityt &array_equality)
{
  // add constraints x=y => x[i]=y[i]

  // Also collect element-equality literals for the reverse direction
  // (index-set extensionality): /\(x[i]=y[i]) => x=y
  bvt elem_eq_lits;

  for(const auto &index : index_set)
  {
    const typet &element_type1 =
      to_array_type(array_equality.f1.type()).element_type();
    index_exprt index_expr1(array_equality.f1, index, element_type1);

    const typet &element_type2 =
      to_array_type(array_equality.f2.type()).element_type();
    index_exprt index_expr2(array_equality.f2, index, element_type2);

    DATA_INVARIANT(
      index_expr1.type()==index_expr2.type(),
      "array elements should all have same type");

    equal_exprt equality_expr(index_expr1, index_expr2);

    // add constraint: l -> x[i]=y[i]
    // convert must be done to guarantee correct update of the index_set
    literalt eq_lit = convert(equality_expr);
    prop.lcnf(!array_equality.l, eq_lit);
    array_constraint_count[constraint_typet::ARRAY_EQUALITY]++;

    elem_eq_lits.push_back(eq_lit);
  }

  // Index-set extensionality (reverse direction):
  // /\(x[i]=y[i]) -> l, i.e., !x[i1]=y[i1] \/ !x[i2]=y[i2] \/ ... \/ l
  // This is incomplete (arrays might differ at indices not in the set)
  // but combined with the Skolem diff index it provides a complete
  // encoding while allowing the SAT solver to use this cheaper clause
  // for propagation.
  if(array_equality.l != const_literal(true) && !elem_eq_lits.empty())
  {
    bvt clause;
    clause.reserve(elem_eq_lits.size() + 1);
    for(const auto &lit : elem_eq_lits)
      clause.push_back(!lit);
    clause.push_back(array_equality.l);
    prop.lcnf(clause);
  }
}

void arrayst::add_array_constraints(
  const index_sett &index_set,
  const exprt &expr)
{
  if(expr.id()==ID_with)
    return add_array_constraints_with(index_set, to_with_expr(expr));
  else if(expr.id()==ID_update)
    return add_array_constraints_update(index_set, to_update_expr(expr));
  else if(expr.id()==ID_if)
    return add_array_constraints_if(index_set, to_if_expr(expr));
  else if(expr.id()==ID_array_of)
    return add_array_constraints_array_of(index_set, to_array_of_expr(expr));
  else if(expr.id() == ID_array)
    return add_array_constraints_array_constant(index_set, to_array_expr(expr));
  else if(expr.id() == ID_array_comprehension)
  {
    return add_array_constraints_comprehension(
      index_set, to_array_comprehension_expr(expr));
  }
  else if(
    expr.id() == ID_symbol || expr.id() == ID_nondet_symbol ||
    expr.is_constant() || expr.id() == "zero_string" ||
    expr.id() == ID_string_constant)
  {
  }
  else if(expr.id() == ID_member)
  {
  }
  else if(expr.id()==ID_byte_update_little_endian ||
          expr.id()==ID_byte_update_big_endian)
  {
    INVARIANT(false, "byte_update should be removed before arrayst");
  }
  else if(expr.id()==ID_typecast)
  {
    // we got a=(type[])b
    const auto &expr_typecast_op = to_typecast_expr(expr).op();

    const typet &dest_element_type = to_array_type(expr.type()).element_type();
    const typet &src_element_type =
      to_array_type(expr_typecast_op.type()).element_type();

    // When element types differ in size (e.g., SIMD vector reinterpretation
    // casts like int32[4] <-> int64[2]), the element-wise constraint
    // a[i]=b[i] is incorrect. The bitvector-level conversion handles
    // these as bitwise copies, so skip the array-level constraint.
    if(dest_element_type == src_element_type)
    {
      // add a[i]=b[i]
      for(const auto &index : index_set)
      {
        index_exprt index_expr1(expr, index, dest_element_type);
        index_exprt index_expr2(expr_typecast_op, index, dest_element_type);

        DATA_INVARIANT(
          index_expr1.type() == index_expr2.type(),
          "array elements should all have same type");

        // add constraint
        lazy_constraintt lazy(
          lazy_typet::ARRAY_TYPECAST, equal_exprt(index_expr1, index_expr2));
        add_array_constraint(lazy, false); // added immediately
        array_constraint_count[constraint_typet::ARRAY_TYPECAST]++;
      }
    }
  }
  else if(expr.id()==ID_index)
  {
  }
  else if(auto let_expr = expr_try_dynamic_cast<let_exprt>(expr))
  {
    // we got x=let(a=e, A)
    // add x[i]=A[a/e][i]

    exprt where = let_expr->where();
    replace_symbolt replace_symbol;
    for(const auto &binding :
        make_range(let_expr->variables()).zip(let_expr->values()))
    {
      replace_symbol.insert(binding.first, binding.second);
    }
    replace_symbol(where);

    for(const auto &index : index_set)
    {
      index_exprt index_expr{expr, index};
      index_exprt where_indexed{where, index};

      // add constraint
      lazy_constraintt lazy{
        lazy_typet::ARRAY_LET, equal_exprt{index_expr, where_indexed}};

      add_array_constraint(lazy, false); // added immediately
      array_constraint_count[constraint_typet::ARRAY_LET]++;
    }
  }
  else
  {
    DATA_INVARIANT(
      false,
      "unexpected array expression (add_array_constraints): '" +
        expr.id_string() + "'");
  }
}

void arrayst::add_array_constraints_with(
  const index_sett &index_set,
  const with_exprt &expr)
{
  // We got x=(y with [i:=v]).
  // First add constraint x[i]=v
  std::unordered_set<exprt, irep_hash> updated_indices;

  index_exprt index_expr(
    expr, expr.where(), to_array_type(expr.type()).element_type());

  DATA_INVARIANT_WITH_DIAGNOSTICS(
    index_expr.type() == expr.new_value().type(),
    "with-expression operand should match array element type",
    irep_pretty_diagnosticst{expr});

  lazy_constraintt lazy(
    lazy_typet::ARRAY_WITH, equal_exprt(index_expr, expr.new_value()));
  add_array_constraint(lazy, false); // added immediately
  array_constraint_count[constraint_typet::ARRAY_WITH]++;

  updated_indices.insert(expr.where());

  // Also add x[I]=v for other indices I that may equal the
  // write index.  This helps propagation when the write index
  // and read index are different SSA symbols connected by
  // equality constraints (e.g., argc'#0 and main_argc).
  for(const auto &other_index : index_set)
  {
    if(other_index == expr.where())
      continue;

    const literalt idx_eq = convert(equal_exprt(
      other_index,
      typecast_exprt::conditional_cast(expr.where(), other_index.type())));

    if(idx_eq.is_false())
      continue;

    index_exprt other_read(
      expr, other_index, to_array_type(expr.type()).element_type());
    lazy_constraintt lazy2(
      lazy_typet::ARRAY_WITH,
      implies_exprt(
        literal_exprt(idx_eq), equal_exprt(other_read, expr.new_value())));
    add_array_constraint(lazy2, false);
    array_constraint_count[constraint_typet::ARRAY_WITH]++;
  }

  // For all other indices use the existing value, i.e., add constraints
  // x[I]=y[I] for I!=i,j,...

  for(auto other_index : index_set)
  {
    if(updated_indices.find(other_index) == updated_indices.end())
    {
      // we first build the guard
      exprt::operandst disjuncts;
      disjuncts.reserve(updated_indices.size());
      for(const auto &index : updated_indices)
      {
        disjuncts.push_back(equal_exprt{
          index, typecast_exprt::conditional_cast(other_index, index.type())});
      }

      literalt guard_lit = convert(disjunction(disjuncts));

      if(guard_lit!=const_literal(true))
      {
        const typet &element_type = to_array_type(expr.type()).element_type();
        index_exprt index_expr1(expr, other_index, element_type);
        index_exprt index_expr2(expr.old(), other_index, element_type);

        equal_exprt equality_expr(index_expr1, index_expr2);

        // add constraint
        lazy_constraintt lazy(lazy_typet::ARRAY_WITH, or_exprt(equality_expr,
                                literal_exprt(guard_lit)));

        add_array_constraint(lazy, false); // added immediately
        array_constraint_count[constraint_typet::ARRAY_WITH]++;

#if 0 // old code for adding, not significantly faster
        {
          literalt equality_lit=convert(equality_expr);

          bvt bv;
          bv.reserve(2);
          bv.push_back(equality_lit);
          bv.push_back(guard_lit);
          prop.lcnf(bv);
        }
#endif
      }
    }
  }
}

void arrayst::add_array_constraints_update(
  const index_sett &,
  const update_exprt &)
{
  // we got x=UPDATE(y, [i], v)
  // add constaint x[i]=v

#if 0
  const exprt &index=expr.where();
  const exprt &value=expr.new_value();

  {
    index_exprt index_expr(expr, index, expr.type().subtype());

    DATA_INVARIANT_WITH_DIAGNOSTICS(
      index_expr.type()==value.type(),
      "update operand should match array element type",
      irep_pretty_diagnosticst{expr});

    set_to_true(equal_exprt(index_expr, value));
  }

  // use other array index applications for "else" case
  // add constraint x[I]=y[I] for I!=i

  for(auto other_index : index_set)
  {
    if(other_index!=index)
    {
      // we first build the guard

      other_index = typecast_exprt::conditional_cast(other_index, index.type());

      literalt guard_lit=convert(equal_exprt(index, other_index));

      if(guard_lit!=const_literal(true))
      {
        const typet &subtype=expr.type().subtype();
        index_exprt index_expr1(expr, other_index, subtype);
        index_exprt index_expr2(expr.op0(), other_index, subtype);

        equal_exprt equality_expr(index_expr1, index_expr2);

        literalt equality_lit=convert(equality_expr);

        // add constraint
        bvt bv;
        bv.reserve(2);
        bv.push_back(equality_lit);
        bv.push_back(guard_lit);
        prop.lcnf(bv);
      }
    }
  }
#endif
}

void arrayst::add_array_constraints_array_of(
  const index_sett &index_set,
  const array_of_exprt &expr)
{
  // we got x=array_of[v]
  // get other array index applications
  // and add constraint x[i]=v

  for(const auto &index : index_set)
  {
    const typet &element_type = expr.type().element_type();
    index_exprt index_expr(expr, index, element_type);

    DATA_INVARIANT(
      index_expr.type() == expr.what().type(),
      "array_of operand type should match array element type");

    // add constraint
    lazy_constraintt lazy(
      lazy_typet::ARRAY_OF, equal_exprt(index_expr, expr.what()));
    add_array_constraint(lazy, false); // added immediately
    array_constraint_count[constraint_typet::ARRAY_OF]++;
  }
}

void arrayst::add_array_constraints_array_constant(
  const index_sett &index_set,
  const array_exprt &expr)
{
  // we got x = { v, ... } - add constraint x[i] = v
  const exprt::operandst &operands = expr.operands();

  for(const auto &index : index_set)
  {
    const typet &element_type = expr.type().element_type();
    const index_exprt index_expr{expr, index, element_type};

    if(index.is_constant())
    {
      // We have a constant index - just pick the element at that index from the
      // array constant.

      const std::optional<std::size_t> i =
        numeric_cast<std::size_t>(to_constant_expr(index));
      // if the access is out of bounds, we leave it unconstrained
      if(!i.has_value() || *i >= operands.size())
        continue;

      const exprt v = operands[*i];
      DATA_INVARIANT(
        index_expr.type() == v.type(),
        "array operand type should match array element type");

      // add constraint
      lazy_constraintt lazy{lazy_typet::ARRAY_CONSTANT,
                            equal_exprt{index_expr, v}};
      add_array_constraint(lazy, false); // added immediately
      array_constraint_count[constraint_typet::ARRAY_CONSTANT]++;
    }
    else
    {
      // We have a non-constant index into an array constant. We need to build a
      // case statement testing the index against all possible values. Whenever
      // neighbouring array elements are the same, we can test the index against
      // the range rather than individual elements. This should be particularly
      // helpful when we have arrays of zeros, as is the case for initializers.

      std::vector<std::pair<std::size_t, std::size_t>> ranges;

      for(std::size_t i = 0; i < operands.size(); ++i)
      {
        if(ranges.empty() || operands[i] != operands[ranges.back().first])
          ranges.emplace_back(i, i);
        else
          ranges.back().second = i;
      }

      for(const auto &range : ranges)
      {
        exprt index_constraint;

        if(range.first == range.second)
        {
          index_constraint =
            equal_exprt{index, from_integer(range.first, index.type())};
        }
        else
        {
          index_constraint = and_exprt{
            binary_predicate_exprt{
              from_integer(range.first, index.type()), ID_le, index},
            binary_predicate_exprt{
              index, ID_le, from_integer(range.second, index.type())}};
        }

        lazy_constraintt lazy{
          lazy_typet::ARRAY_CONSTANT,
          implies_exprt{index_constraint,
                        equal_exprt{index_expr, operands[range.first]}}};
        add_array_constraint(lazy, true); // added lazily
        array_constraint_count[constraint_typet::ARRAY_CONSTANT]++;
      }
    }
  }
}

void arrayst::add_array_constraints_comprehension(
  const index_sett &index_set,
  const array_comprehension_exprt &expr)
{
  // we got x=lambda(i: e)
  // get all other array index applications
  // and add constraints x[j]=e[i/j]

  for(const auto &index : index_set)
  {
    index_exprt index_expr{expr, index};
    exprt comprehension_body = expr.body();
    replace_expr(expr.arg(), index, comprehension_body);

    // add constraint
    lazy_constraintt lazy(
      lazy_typet::ARRAY_COMPREHENSION,
      equal_exprt(index_expr, comprehension_body));

    add_array_constraint(lazy, false); // added immediately
    array_constraint_count[constraint_typet::ARRAY_COMPREHENSION]++;
  }
}

void arrayst::add_array_constraints_if(
  const index_sett &index_set,
  const if_exprt &expr)
{
  // we got x=(c?a:b)
  literalt cond_lit=convert(expr.cond());

  // get other array index applications
  // and add c => x[i]=a[i]
  //        !c => x[i]=b[i]

  // first do true case

  for(const auto &index : index_set)
  {
    const typet &element_type = to_array_type(expr.type()).element_type();
    index_exprt index_expr1(expr, index, element_type);
    index_exprt index_expr2(expr.true_case(), index, element_type);

    // add implication
    lazy_constraintt lazy(lazy_typet::ARRAY_IF,
                            or_exprt(literal_exprt(!cond_lit),
                              equal_exprt(index_expr1, index_expr2)));
    add_array_constraint(lazy, false); // added immediately
    array_constraint_count[constraint_typet::ARRAY_IF]++;

#if 0 // old code for adding, not significantly faster
    prop.lcnf(!cond_lit, convert(equal_exprt(index_expr1, index_expr2)));
#endif
  }

  // now the false case
  for(const auto &index : index_set)
  {
    const typet &element_type = to_array_type(expr.type()).element_type();
    index_exprt index_expr1(expr, index, element_type);
    index_exprt index_expr2(expr.false_case(), index, element_type);

    // add implication
    lazy_constraintt lazy(
      lazy_typet::ARRAY_IF,
      or_exprt(literal_exprt(cond_lit),
      equal_exprt(index_expr1, index_expr2)));
    add_array_constraint(lazy, false); // added immediately
    array_constraint_count[constraint_typet::ARRAY_IF]++;

#if 0 // old code for adding, not significantly faster
    prop.lcnf(cond_lit, convert(equal_exprt(index_expr1, index_expr2)));
#endif
  }
}

std::string map_theoryt::enum_to_string(constraint_typet type)
{
  switch(type)
  {
  case constraint_typet::ARRAY_ACKERMANN:
    return "arrayAckermann";
  case constraint_typet::ARRAY_WITH:
    return "arrayWith";
  case constraint_typet::ARRAY_IF:
    return "arrayIf";
  case constraint_typet::ARRAY_OF:
    return "arrayOf";
  case constraint_typet::ARRAY_TYPECAST:
    return "arrayTypecast";
  case constraint_typet::ARRAY_CONSTANT:
    return "arrayConstant";
  case constraint_typet::ARRAY_COMPREHENSION:
    return "arrayComprehension";
  case constraint_typet::ARRAY_EQUALITY:
    return "arrayEquality";
  case constraint_typet::ARRAY_LET:
    return "arrayLet";
  default:
    UNREACHABLE;
  }
}

void map_theoryt::display_array_constraint_count()
{
  json_objectt json_result;
  json_objectt &json_array_theory =
    json_result["arrayConstraints"].make_object();

  size_t num_constraints = 0;

  array_constraint_countt::iterator it = array_constraint_count.begin();
  while(it != array_constraint_count.end())
  {
    std::string contraint_type_string = enum_to_string(it->first);
    json_array_theory[contraint_type_string] =
      json_numbert(std::to_string(it->second));

    num_constraints += it->second;
    it++;
  }

  json_result["numOfConstraints"] =
    json_numbert(std::to_string(num_constraints));
  log.status() << ",\n" << json_result;
}
