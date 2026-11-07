/*******************************************************************\
Module: util

Author: Romain Brenguier, romain.brenguier@diffblue.com

\*******************************************************************/

#include "union_find_replace.h"

#include <vector>

/// Merge the set containing `a` and the set containing `b`.
/// \param a: an expression
/// \param b: an expression
/// \return current representative element of the set containing `a` and `b`
exprt union_find_replacet::make_union(const exprt &a, const exprt &b)
{
  const exprt &lhs_root = find(a);
  const exprt &rhs_root = find(b);
  if(lhs_root != rhs_root)
  {
    map[lhs_root] = rhs_root;
    replaced.clear();
  }
  return rhs_root;
}

/// Replace subexpressions of `expr` by the representative element of the set
/// they belong to.
/// \param expr: an expression, modified in place
/// \return true if expr is left unchanged
bool union_find_replacet::replace_expr(exprt &expr) const
{
  exprt result = normal_form(expr);
  if(&result.read() == &expr.read())
    return true;
  expr = std::move(result);
  return false;
}

/// The result of replacing subexpressions until a fixpoint, as repeated
/// passes of ::replace_expr do: a subexpression that is in the map is
/// replaced (and its replacement in turn), and otherwise its operands are,
/// after which it is checked again.
exprt union_find_replacet::normal_form(const exprt &expr) const
{
  const void *const node = &expr.read();
  const auto found = replaced.find(node);
  if(found != replaced.end())
    return found->second.second;
  exprt result;
  const auto it = map.find(expr);
  if(it != map.end())
    result = normal_form(it->second);
  else
  {
    result = expr;
    for(std::size_t i = 0; i < expr.operands().size(); ++i)
    {
      exprt op = normal_form(expr.operands()[i]);
      if(&op.read() != &expr.operands()[i].read())
        result.operands()[i] = std::move(op);
    }
    if(&result.read() != &expr.read())
    {
      const auto again = map.find(result);
      if(again != map.end())
        result = normal_form(again->second);
    }
  }
  replaced.emplace(node, std::make_pair(expr, result));
  return result;
}

/// \param expr: an expression
/// \return representative element of the set `expr` belongs to
exprt union_find_replacet::find(exprt expr) const
{
  // Follow the path from expr itself to the end, and make each expression
  // on it point to the end: following paths in replace_expr, which
  // replaces subexpressions until a fixpoint, took one pass for each
  // expression on a path, and paths may be as long as the number of
  // unions.
  auto it = map.find(expr);
  if(it != map.end())
  {
    std::vector<replace_mapt::iterator> path;
    while(it != map.end())
    {
      path.push_back(it);
      it = map.find(it->second);
    }
    const exprt end = path.back()->second;
    for(auto &on_path : path)
      on_path->second = end;
    expr = end;
  }
  replace_expr(expr);
  return expr;
}

/// \return pairs of expression composed of expressions and a representative
///   expression for the set they below to.
std::vector<std::pair<exprt, exprt>> union_find_replacet::to_vector() const
{
  std::vector<std::pair<exprt, exprt>> equations;
  for(const auto &pair : map)
    equations.emplace_back(pair.first, find(pair.second));
  return equations;
}
