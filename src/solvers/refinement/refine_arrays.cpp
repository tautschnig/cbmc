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

  // ============================================================
  // PHASE 1: Collect all model values (no clause modifications)
  // CaDiCaL requires satisfied state for model queries.
  // ============================================================

  // 1a. Lazy select: collect with-selects for bit-blasting
  struct select_violationt
  {
    bvt lazy_bv;
    index_exprt expr;
  };
  std::vector<select_violationt> select_violations;
  std::vector<std::size_t> to_remove;
  for(std::size_t si = 0; si < lazy_selects.size(); si++)
  {
    if(lazy_selects[si].expr.array().id() == ID_with)
    {
      select_violations.push_back({lazy_selects[si].bv, lazy_selects[si].expr});
      to_remove.push_back(si);
    }
  }

  // 1a-cont. Element-wise constraint violations
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
    if(current.id() == ID_implies)
    {
      if(get_value(to_implies_expr(current).op0()) == false_exprt())
        continue;
    }
    if(current.id() == ID_or)
    {
      const or_exprt &orexp = to_or_expr(current);
      INVARIANT(orexp.operands().size() == 2, "binary or");
      if(
        get_value(orexp.op0()) == true_exprt() ||
        get_value(orexp.op1()) == true_exprt())
        continue;
    }
    if(!lazy_selects.empty())
      to_check.push_back({current, false_exprt(), it});
    else
      to_check.push_back({current, simplify_expr(get_value(current), ns), it});
  }

  // 1c. Ackermann violations
  struct ackermann_violationt
  {
    exprt idx1, idx2, arr;
    typet element_type;
  };
  std::vector<ackermann_violationt> ackermann_violations;
  for(std::size_t i = 0; i < arrays.size(); i++)
  {
    if(arrays.find_number(i) != i)
      continue;
    const index_sett &idx_set = index_map[i];
    if(idx_set.size() < 2)
      continue;
    const typet &et = to_array_type(arrays[i].type()).element_type();
    std::map<exprt, std::vector<exprt>> val_to_idx;
    for(const auto &idx : idx_set)
      val_to_idx[get_value(idx)].push_back(idx);
    for(const auto &[val, indices] : val_to_idx)
    {
      if(indices.size() < 2)
        continue;
      for(std::size_t a = 0; a < arrays.size(); a++)
      {
        if(arrays.find_number(a) != i)
          continue;
        const exprt v0 = get_value(index_exprt{arrays[a], indices[0], et});
        for(std::size_t k = 1; k < indices.size(); k++)
        {
          if(get_value(index_exprt{arrays[a], indices[k], et}) != v0)
            ackermann_violations.push_back(
              {indices[0], indices[k], arrays[a], et});
        }
      }
    }
  }

  // 1d. Extensionality candidates
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
      continue;
    if(prop.l_get(equality.l).is_true())
      continue;
    const std::size_t root = arrays.find_number(equality.f1);
    const index_sett &idx_set = index_map[root];
    const typet &et = to_array_type(equality.f1.type()).element_type();
    bool all_eq = true;
    for(const auto &index : idx_set)
    {
      if(
        get_value(index_exprt{equality.f1, index, et}) !=
        get_value(index_exprt{equality.f2, index, et}))
      {
        all_eq = false;
        break;
      }
    }
    if(all_eq)
      ext_candidates.push_back({equality.l, equality.f1, equality.f2});
  }

  // ============================================================
  // PHASE 2: Add clauses (model is now invalidated)
  // ============================================================

  const bool was_lazy = lazy_arrays;
  lazy_arrays = false;

  // 2a. Lazy selects
  for(const auto &v : select_violations)
  {
    bv_cache.erase(v.expr);
    bvt ite_bv = convert_bv(v.expr);
    for(std::size_t i = 0; i < v.lazy_bv.size() && i < ite_bv.size(); i++)
    {
      prop.lcnf(!v.lazy_bv[i], ite_bv[i]);
      prop.lcnf(v.lazy_bv[i], !ite_bv[i]);
    }
    nb_active++;
  }
  for(auto it = to_remove.rbegin(); it != to_remove.rend(); ++it)
    lazy_selects.erase(lazy_selects.begin() + *it);
  if(!select_violations.empty())
  {
    log.debug() << "BV-Refinement: " << select_violations.size()
                << " lazy selects bit-blasted" << messaget::eom;
    progress = true;
  }

  // 2a-cont. Element-wise constraints
  static const unsigned MAX_ACTIVATIONS = 100;
  for(auto &entry : to_check)
  {
    if(entry.simplified == false_exprt())
    {
      prop.l_set_to_true(convert(entry.constraint));
      nb_active++;
      lazy_array_constraints.erase(entry.list_it);
      if(!lazy_selects.empty())
        continue; // no limit when force-activating with lazy selects
      if(nb_active >= MAX_ACTIVATIONS)
        break;
    }
  }
  log.debug() << "BV-Refinement: " << nb_active
              << " array expressions become active" << messaget::eom;
  log.debug() << "BV-Refinement: " << lazy_array_constraints.size()
              << " inactive array expressions" << messaget::eom;
  if(nb_active > 0)
    progress = true;

  // 2c. Ackermann constraints
  unsigned nb_ackermann = 0;
  for(const auto &v : ackermann_violations)
  {
    prop.l_set_to_true(convert(implies_exprt{
      equal_exprt{
        v.idx1, typecast_exprt::conditional_cast(v.idx2, v.idx1.type())},
      equal_exprt{
        index_exprt{v.arr, v.idx1, v.element_type},
        index_exprt{v.arr, v.idx2, v.element_type}}}));
    nb_ackermann++;
    if(lazy_selects.empty() && nb_ackermann >= MAX_ACTIVATIONS)
      break;
  }
  if(nb_ackermann > 0)
  {
    log.debug() << "BV-Refinement: " << nb_ackermann
                << " Ackermann constraints added" << messaget::eom;
    progress = true;
  }

  // 2d. Extensionality diff indices
  unsigned nb_ext = 0;
  for(const auto &cand : ext_candidates)
  {
    const array_typet &array_type = to_array_type(cand.f1.type());
    typet index_type = array_type.index_type();
    if(
      can_cast_type<bitvector_typet>(index_type) &&
      to_bitvector_type(index_type).get_width() == 0)
      index_type = array_type.size().type();
    const typet &et = array_type.element_type();
    const symbol_exprt diff_index{
      "array_theory::diff#" + std::to_string(extensionality_counter++),
      index_type};
    const std::size_t root = arrays.find_number(cand.f1);
    index_map[root].insert(diff_index);
    update_index_map(true);
    for(std::size_t i = 0; i < arrays.size(); i++)
    {
      if(arrays.find_number(i) != root)
        continue;
      add_array_constraints(index_sett{diff_index}, arrays[i]);
    }
    const literalt eq_lit = convert(equal_exprt{
      index_exprt{cand.f1, diff_index, et},
      index_exprt{cand.f2, diff_index, et}});
    prop.lcnf(!eq_lit, cand.l);
    prop.lcnf(!cand.l, eq_lit);
    // Add equality constraints for the new diff index.
    // add_array_constraints(index_sett, expr) only creates with/if/etc
    // constraints, not equality constraints between arrays connected by
    // equality edges. Without these, the refinement loop cannot establish
    // that arrays agree at the diff index via the asserted equality,
    // which is required for the extensionality proof.
    for(const auto &equality : array_equalities)
    {
      if(arrays.find_number(equality.f1) != root)
        continue;
      add_array_constraints_equality(index_sett{diff_index}, equality);
    }

    for(const auto &sym : find_symbols(diff_index))
    {
      if(!bv_width.get_width_opt(sym.type()).has_value())
        continue;
      for(const auto &lit : convert_bv(sym))
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

  lazy_arrays = was_lazy;
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
