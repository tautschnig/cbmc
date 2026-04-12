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
  arrayst(
    const namespacet &_ns,
    propt &_prop,
    message_handlert &message_handler,
    bool get_array_constraints = false);

  // NOLINTNEXTLINE(readability/identifiers)
  typedef map_theoryt SUB;

  literalt record_array_equality(const equal_exprt &expr);

  /// When true, use read-over-weakeq (WEG-based) instead of
  /// element-wise + Ackermann constraints.
  bool use_read_over_weakeq = false;

  /// Record that \p symbol is equal to \p value for the purposes of the
  /// array theory. For unbounded-array-typed bindings this connects the
  /// two expressions in the union-find so that element-wise constraints
  /// propagate correctly.
  void record_array_let_binding(const symbol_exprt &symbol, const exprt &value);

protected:
  message_handlert &message_handler;

  void finish_eager_conversion_arrays() override
  {
    add_array_constraints();
  }

  // the list of all equalities between arrays
  // references to objects in this container need to be stable as
  // elements are added while references are held

  // this is used to find the clusters of arrays being compared

  /// Weak equivalence graph (Christ & Hoenicke). Built alongside the
  /// union-find; used for read-over-weakeq constraint generation.
  weak_equivalence_grapht weg;

  // this tracks the array indicies for each array
  // references to values in this container need to be stable as
  // elements are added while references are held

  // adds array constraints lazily

  // adds all the constraints eagerly
  void add_array_constraints();
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

    // (maybe this function should be partially moved here from boolbv)
};

#endif // CPROVER_SOLVERS_FLATTENING_ARRAYS_H
