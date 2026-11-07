/*******************************************************************\

Module: util

Author: Romain Brenguier, romain.brenguier@diffblue.com

\*******************************************************************/

#ifndef CPROVER_UTIL_UNION_FIND_REPLACE_H
#define CPROVER_UTIL_UNION_FIND_REPLACE_H

#include "replace_expr.h"

/// Similar interface to union-find for expressions, with a function for
/// replacing sub-expressions by their result for find.
class union_find_replacet
{
public:
  bool replace_expr(exprt &expr) const;

  exprt find(exprt expr) const;

  exprt make_union(const exprt &a, const exprt &b);

  std::vector<std::pair<exprt, exprt>> to_vector() const;

private:
  /// From each expression to another of its set, towards its
  /// representative. find shortens the paths that it follows, which
  /// does not change the representatives.
  mutable replace_mapt map;
  /// The results of replace_expr by node, with the original kept alive so
  /// that its address is not reused, until the next union: the string
  /// solver replaces in the same large expressions, which share their
  /// subexpressions, many times.
  mutable std::unordered_map<const void *, std::pair<exprt, exprt>> replaced;
  exprt normal_form(const exprt &expr) const;
};

#endif // CPROVER_UTIL_UNION_FIND_REPLACE_H
