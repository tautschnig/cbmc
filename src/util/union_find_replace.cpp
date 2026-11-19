/*******************************************************************\
Module: util

Author: Romain Brenguier, romain.brenguier@diffblue.com

\*******************************************************************/

#include "union_find_replace.h"

#include <functional>
#include <unordered_map>

/// Merge the set containing `a` and the set containing `b`.
/// \param a: an expression
/// \param b: an expression
/// \return current representative element of the set containing `a` and `b`
exprt union_find_replacet::make_union(const exprt &a, const exprt &b)
{
  const exprt &lhs_root = find(a);
  const exprt &rhs_root = find(b);
  if(lhs_root != rhs_root)
    map[lhs_root] = rhs_root;
  return rhs_root;
}

/// Replace subexpressions of `expr` by the representative element of the set
/// they belong to.
/// \param expr: an expression, modified in place
/// \return true if expr is left unchanged
bool union_find_replacet::replace_expr(exprt &expr) const
{
  // Replace every sub-expression that is in the map by its representative,
  // and the representative's sub-expressions in turn, to a fixpoint (the
  // map is acyclic). Shared sub-expressions are rewritten once (memoised):
  // a plain tree traversal visits a shared node once per path to it, which
  // is exponential in the depth of DAG-shaped expressions (merges of merges
  // of pointers) and exhausted memory on string solver inputs.
  if(map.empty())
    return true;
  std::unordered_map<exprt, exprt, irep_hash> memo;
  std::function<const exprt &(const exprt &)> rec =
    [&](const exprt &e) -> const exprt &
  {
    auto mi = memo.find(e);
    if(mi != memo.end())
      return mi->second;
    exprt result;
    auto findit = map.find(e);
    if(findit != map.end())
    {
      PRECONDITION_WITH_DIAGNOSTICS(
        e.type() == findit->second.type(),
        "types to be replaced should match. old type:\n" + e.type().pretty() +
          "\nnew type:\n" + findit->second.type().pretty());
      result = rec(findit->second);
    }
    else
    {
      result = e;
      bool changed = false;
      exprt::operandst &ops = result.operands();
      for(std::size_t i = 0; i < ops.size(); ++i)
      {
        const exprt &r = rec(e.operands()[i]);
        if(r != e.operands()[i])
        {
          ops[i] = r;
          changed = true;
        }
      }
      if(!changed)
        result = e;
    }
    return memo.emplace(e, std::move(result)).first->second;
  };
  exprt replaced = rec(expr);
  const bool unchanged = replaced == expr;
  if(!unchanged)
    expr = std::move(replaced);
  return unchanged;
}

/// \param expr: an expression
/// \return representative element of the set `expr` belongs to
exprt union_find_replacet::find(exprt expr) const
{
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
