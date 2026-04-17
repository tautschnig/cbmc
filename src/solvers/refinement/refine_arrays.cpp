/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "bv_refinement.h"

#ifdef DEBUG
#include <iostream>
#endif

#include <util/bitvector_types.h>
#include <util/find_symbols.h>
#include <util/format_expr.h>
#include <util/simplify_expr.h>
#include <util/std_expr.h>

#include <solvers/sat/satcheck.h>

/// generate array constraints
void bv_refinementt::finish_eager_conversion_arrays()
{
  collect_indices();
  // at this point all indices should in the index set

  // just build the data structure
  update_index_map(true);

  // we don't actually add any constraints
  lazy_arrays=config_.refine_arrays;
  add_array_constraints();
  freeze_lazy_constraints();
}

/// check whether counterexample is spurious
void bv_refinementt::arrays_overapproximated()
{
  if(!config_.refine_arrays)
    return;

  unsigned nb_active=0;

  // Evaluate all lazy constraints while the solver is still in SAT state.
  // We must not interleave get_value() calls with modifications to the
  // main solver (prop) because some SAT solvers (e.g., CaDiCaL) only
  // permit reading model values while in the satisfied state, and adding
  // clauses invalidates that state.
  struct evaluated_constraintt
  {
    exprt constraint;
    exprt simplified;
    std::list<lazy_constraintt>::iterator list_it;
  };
  std::vector<evaluated_constraintt> to_check;
  to_check.reserve(lazy_array_constraints.size());

  for(auto it = lazy_array_constraints.begin();
      it != lazy_array_constraints.end();
      ++it)
  {
    const exprt &current = it->lazy;

    // some minor simplifications
    // check if they are worth having
    if(current.id()==ID_implies)
    {
      implies_exprt imp=to_implies_expr(current);
      exprt implies_simplified = get_value(imp.op0());
      if(implies_simplified==false_exprt())
      {
        continue;
      }
    }

    if(current.id()==ID_or)
    {
      or_exprt orexp=to_or_expr(current);
      INVARIANT(
        orexp.operands().size() == 2, "only treats the case of a binary or");
      exprt o1 = get_value(orexp.op0());
      exprt o2 = get_value(orexp.op1());
      if(o1==true_exprt() || o2 == true_exprt())
      {
        continue;
      }
    }

    to_check.push_back({current, simplify_expr(get_value(current), ns), it});
  }

  // Check each evaluated constraint against the model.
  // If violated, convert and assert it (truly lazy: first time bit-blasting).
  static const unsigned MAX_ACTIVATIONS = 100;
  for(auto &entry : to_check)
  {
    if(entry.simplified == false_exprt())
    {
      // Convert and permanently assert the violated constraint
      prop.l_set_to_true(convert(entry.constraint));
      nb_active++;
      lazy_array_constraints.erase(entry.list_it);
      if(nb_active >= MAX_ACTIVATIONS)
        break;
    }
  }

  log.debug() << "BV-Refinement: " << nb_active
              << " array expressions become active" << messaget::eom;
  log.debug() << "BV-Refinement: " << lazy_array_constraints.size()
              << " inactive array expressions" << messaget::eom;
  if(nb_active > 0)
    progress=true;

  // Extensionality refinement: check if any array equality literal is
  // false while all element equalities at known indices are true.
  // If so, the extensionality axiom is violated — add a Skolem diff
  // index for that equality and generate constraints for it.
  unsigned nb_ext = 0;

  // First pass: evaluate all equality literals while solver is in SAT state.
  struct ext_candidatet
  {
    literalt l;
    exprt f1, f2;
  };
  std::vector<ext_candidatet> ext_candidates;

  for(const auto &equality : array_equalities)
  {
    if(equality.l == const_literal(true))
      continue;
    if(equality.asserted_true || asserted_true_literals.count(equality.l.get()))
    {
      continue;
    }

    // Check if the equality literal is false in the current model
    const tvt l_val = prop.l_get(equality.l);
    if(l_val.is_true())
      continue;

    // The equality is false — check if all elements at known indices agree
    const std::size_t root = arrays.find_number(equality.f1);
    const index_sett &idx_set = index_map[root];
    const typet &element_type =
      to_array_type(equality.f1.type()).element_type();

    bool all_elements_equal = true;
    for(const auto &index : idx_set)
    {
      const index_exprt e1{equality.f1, index, element_type};
      const index_exprt e2{equality.f2, index, element_type};
      const exprt v1 = get_value(e1);
      const exprt v2 = get_value(e2);
      if(v1 != v2)
      {
        all_elements_equal = false;
        break;
      }
    }

    if(all_elements_equal)
      ext_candidates.push_back({equality.l, equality.f1, equality.f2});
  }

  // Second pass: add diff indices (modifies the solver).
  for(const auto &cand : ext_candidates)
  {
    const array_typet &array_type = to_array_type(cand.f1.type());
    typet index_type = array_type.index_type();
    if(
      can_cast_type<bitvector_typet>(index_type) &&
      to_bitvector_type(index_type).get_width() == 0)
    {
      index_type = array_type.size().type();
    }
    const typet &element_type = array_type.element_type();

    const irep_idt diff_id =
      "array_theory::diff#" + std::to_string(extensionality_counter++);
    const symbol_exprt diff_index{diff_id, index_type};

    // Add diff to index set
    const std::size_t root = arrays.find_number(cand.f1);
    index_map[root].insert(diff_index);
    update_index_map(true);

    // Generate element-wise constraints for the diff index
    for(std::size_t i = 0; i < arrays.size(); i++)
    {
      if(arrays.find_number(i) != root)
        continue;
      const index_sett diff_set{diff_index};
      add_array_constraints(diff_set, arrays[i]);
    }

    // Extensionality constraint: f1[diff] = f2[diff] -> l
    const index_exprt elem1{cand.f1, diff_index, element_type};
    const index_exprt elem2{cand.f2, diff_index, element_type};
    const literalt elem_eq_lit = convert(equal_exprt{elem1, elem2});
    prop.lcnf(!elem_eq_lit, cand.l);

    // Equality propagation: l -> f1[diff] = f2[diff]
    prop.lcnf(!cand.l, elem_eq_lit);

    // Freeze the new variables
    for(const auto &sym : find_symbols(diff_index))
    {
      if(!bv_width.get_width_opt(sym.type()).has_value())
        continue;
      const bvt bv = convert_bv(sym);
      for(const auto &lit : bv)
        if(!lit.is_constant())
          prop.set_frozen(lit);
    }

    nb_ext++;
    log.debug() << "BV-Refinement: extensionality diff index added for "
                << format(cand.f1) << " vs " << format(cand.f2)
                << messaget::eom;
  }

  if(nb_ext > 0)
  {
    log.debug() << "BV-Refinement: " << nb_ext
                << " extensionality constraints added" << messaget::eom;
    progress = true;
  }
}


/// freeze symbols for incremental solving
void bv_refinementt::freeze_lazy_constraints()
{
  if(!lazy_arrays)
    return;

  // Convert all array index expressions in lazy constraints so the
  // SAT model has meaningful values for them. This bit-blasts the
  // select expressions (creating ITE muxes) without asserting the
  // constraints that connect them.
  for(const auto &constraint : lazy_array_constraints)
  {
    constraint.lazy.visit_pre(
      [&](const exprt &e)
      {
        if(e.id() == ID_index && bv_width.get_width_opt(e.type()).has_value())
        {
          const bvt bv = convert_bv(e);
          for(const auto &lit : bv)
            if(!lit.is_constant())
              prop.set_frozen(lit);
        }
      });
  }
}
