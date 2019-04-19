/*******************************************************************\

Module: Constant propagation

Author: Peter Schrammel

\*******************************************************************/

/// \file
/// Constant propagation
///
/// A simple, unsound constant propagator. Assignments to symbols (local and
/// global variables) are tracked, and propagated if a unique value is found
/// at a given use site. Function calls are accounted for (they are assumed to
/// overwrite all address-taken variables; see \ref dirtyt), but assignments
/// through pointers are not (they are assumed to have no effect).
///
/// Can be restricted to operate over only particular symbols by passing a
/// predicate to a \ref constant_propagator_ait constructor, in which case this
/// can be rendered sound by restricting it to non-address-taken variables.

#ifndef CPROVER_ANALYSES_CONSTANT_PROPAGATOR_H
#define CPROVER_ANALYSES_CONSTANT_PROPAGATOR_H

#include <util/constant_propagation_utils.h>

#include "ai.h"
#include "dirty.h"

class constant_propagator_ait;

class constant_propagator_domaint:public ai_domain_baset
{
public:
  virtual void transform(
    const irep_idt &function_from,
    locationt from,
    const irep_idt &function_to,
    locationt to,
    ai_baset &ai_base,
    const namespacet &ns) final override;

  virtual void output(
    std::ostream &out,
    const ai_baset &ai_base,
    const namespacet &ns) const override;

  bool merge(
    const constant_propagator_domaint &other,
    locationt from,
    locationt to);

  virtual bool ai_simplify(
    exprt &condition,
    const namespacet &ns) const final override;

  virtual void make_bottom() final override
  {
    values.set_to_bottom();
  }

  virtual void make_top() final override
  {
    values.set_to_top();
  }

  virtual void make_entry() final override
  {
    make_top();
  }

  virtual bool is_bottom() const final override
  {
    return values.is_bot();
  }

  virtual bool is_top() const final override
  {
    return values.is_top();
  }

  constant_valuest values;

protected:
  static void assign_rec(
    constant_valuest &dest_values,
    const exprt &lhs,
    const exprt &rhs,
    const namespacet &ns,
    const constant_propagator_ait *cp,
    bool is_assignment);

  bool two_way_propagate_rec(
    const exprt &expr,
    const namespacet &ns,
    const constant_propagator_ait *cp);
};

class constant_propagator_ait:public ait<constant_propagator_domaint>
{
public:
  typedef std::function<bool(const exprt &, const namespacet &)>
    should_track_valuet;

  static bool track_all_values(const exprt &, const namespacet &)
  {
    return true;
  }

  explicit constant_propagator_ait(
    const goto_functionst &goto_functions,
    should_track_valuet should_track_value = track_all_values):
    dirty(goto_functions),
    should_track_value(should_track_value)
  {
  }

  explicit constant_propagator_ait(
    const goto_functiont &goto_function,
    should_track_valuet should_track_value = track_all_values):
    dirty(goto_function),
    should_track_value(should_track_value)
  {
  }

  constant_propagator_ait(
    goto_modelt &goto_model,
    should_track_valuet should_track_value = track_all_values):
    dirty(goto_model.goto_functions),
    should_track_value(should_track_value)
  {
    const namespacet ns(goto_model.symbol_table);
    operator()(goto_model.goto_functions, ns);
    replace(goto_model.goto_functions, ns);
  }

  constant_propagator_ait(
    const irep_idt &function_identifier,
    goto_functionst::goto_functiont &goto_function,
    const namespacet &ns,
    should_track_valuet should_track_value = track_all_values)
    : dirty(goto_function), should_track_value(should_track_value)
  {
    operator()(function_identifier, goto_function, ns);
    replace(goto_function, ns);
  }

  dirtyt dirty;

protected:
  friend class constant_propagator_domaint;

  void replace(
    goto_functionst::goto_functiont &,
    const namespacet &);

  void replace(
    goto_functionst &,
    const namespacet &);

  should_track_valuet should_track_value;
};

#endif // CPROVER_ANALYSES_CONSTANT_PROPAGATOR_H
