/*******************************************************************\

Module: Constant propagation -- utility functions

Author: Peter Schrammel

\*******************************************************************/

/// \file
/// Constant propagation utilities

#ifndef CPROVER_UTIL_CONSTANT_PROPAGATION_UTILS_H
#define CPROVER_UTIL_CONSTANT_PROPAGATION_UTILS_H

#include "replace_symbol.h"

#include <iosfwd>
#include <unordered_set>

class namespacet;

class constant_valuest
{
public:
  /// join
  /// \return Return true if "this" has changed.
  bool merge(const constant_valuest &src);

  /// meet
  /// \return Return true if "this" has changed.
  bool meet(const constant_valuest &src, const namespacet &ns);

  // set whole state

  void set_to_bottom()
  {
    replace_const.clear();
    is_bottom = true;
  }

  void set_to_top()
  {
    replace_const.clear();
    is_bottom = false;
  }

  bool is_bot() const
  {
    return is_bottom && replace_const.empty();
  }

  bool is_top() const
  {
    return !is_bottom && replace_const.empty();
  }

  void set_to(const symbol_exprt &lhs, const exprt &rhs)
  {
    replace_const.set(lhs, rhs);
    is_bottom = false;
  }

  bool set_to_top(const symbol_exprt &expr);

  void set_dirty_to_top(
    const std::unordered_set<irep_idt> &dirty_set,
    const namespacet &ns);

  bool is_constant(const exprt &expr) const;

  bool is_constant(const irep_idt &id) const;

  bool is_empty() const
  {
    return replace_const.empty();
  }

  void output(std::ostream &out) const;

  bool partial_evaluate(exprt &expr, const namespacet &ns) const;

  bool replace(exprt &dest) const
  {
    return replace_const.replace(dest);
  }

  bool replace(typet &dest) const
  {
    return static_cast<replace_symbolt const &>(replace_const).replace(dest);
  }

private:
  // maps variables to constants
  address_of_aware_replace_symbolt replace_const;
  bool is_bottom = true;
};

#endif // CPROVER_UTIL_CONSTANT_PROPAGATION_UTILS_H
