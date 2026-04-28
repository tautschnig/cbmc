/*******************************************************************\

Module: Theory of Arrays with Extensionality

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Theory of Arrays with Extensionality

#ifndef CPROVER_SOLVERS_FLATTENING_ARRAYS_H
#define CPROVER_SOLVERS_FLATTENING_ARRAYS_H

#include "map_theory.h"

class array_comprehension_exprt;
class array_exprt;
class array_of_exprt;
class equal_exprt;
class if_exprt;
class index_exprt;
class symbol_exprt;
class with_exprt;
class update_exprt;

class arrayst : public map_theoryt
{
public:
  void enable_lazy_arrays()
  {
    lazy_arrays = true;
    setup_cdclt_propagator();
  }
  arrayst(
    const namespacet &_ns,
    propt &_prop,
    message_handlert &message_handler,
    bool get_array_constraints = false);

  // NOLINTNEXTLINE(readability/identifiers)
  typedef map_theoryt SUB;

  /// Record an array equality a == b. Returns the equality literal.
  /// Creates an equality edge in the WEG.
  literalt record_array_equality(const equal_exprt &expr);
  /// Record that array a is read at index i (from a[i] in the formula).
  /// Adds i to the index set for a's equivalence class.

  /// When true, use read-over-weakeq (WEG-based) instead of
  /// element-wise + Ackermann constraints.
  bool use_read_over_weakeq = false;

  /// Record that \p symbol is equal to \p value for the purposes of the
  /// array theory. For unbounded-array-typed bindings this connects the
  /// two expressions in the union-find so that element-wise constraints
  /// propagate correctly.
  /// Record a let binding symbol == value for an array-typed let.
  /// Creates an equality edge in the WEG connecting the fresh symbol
  /// to the binding value (a store expression).
  void record_array_let_binding(const symbol_exprt &symbol, const exprt &value);

protected:
  message_handlert &message_handler;

  void finish_eager_conversion_arrays() override
  {
    add_array_constraints();
  }

  /// A deferred array select: free BV returned instead of ITE chain.
  /// The refinement loop bit-blasts these by erasing the bv_cache entry
  /// and re-converting with lazy_arrays=false.
  struct lazy_selectt
  {
    bvt bv;
    index_exprt expr;
    bvt index_bv;
  };
  std::vector<lazy_selectt> lazy_selects;

  std::map<exprt, bool> expr_map;

  // adds all the constraints eagerly
  void add_array_constraints();
  // --- Map theory: congruence and extensionality ---
  void add_array_constraints(
    const index_sett &index_set, const exprt &expr);
  void add_array_constraints_if(
    const index_sett &index_set, const if_exprt &exprt);
  void add_array_constraints_with(
    const index_sett &index_set, const with_exprt &expr);
  void add_array_constraints_update(
    const index_sett &index_set, const update_exprt &expr);
  void add_array_constraints_array_of(
    const index_sett &index_set, const array_of_exprt &exprt);
  void add_array_constraints_array_constant(
    const index_sett &index_set,
    const array_exprt &exprt);
  void add_array_constraints_comprehension(
    const index_sett &index_set,
    const array_comprehension_exprt &expr);

  /// For array-of-arrays symbols defined as with-expressions, maps
  /// the symbol to its definition. Used by convert_index to inline
  /// definitions for the 2D ITE encoding.
  std::unordered_map<exprt, exprt, irep_hash> array_2d_definitions;
};

#endif // CPROVER_SOLVERS_FLATTENING_ARRAYS_H
