/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "replace_expr.h"

#include "expr_iterator.h"

#include <functional>

bool replace_expr(const exprt &what, const exprt &by, exprt &dest)
{
  PRECONDITION_WITH_DIAGNOSTICS(
    what.type() == by.type(),
    "types to be replaced should match. old type:\n" + what.type().pretty() +
      "\nnew type:\n" + by.type().pretty());

  bool no_change = true;

  for(auto it = dest.depth_begin(), itend = dest.depth_end();
      it != itend;) // no ++it
  {
    if(*it == what)
    {
      it.mutate() = by;
      no_change = false;
      it.next_sibling_or_parent();
    }
    else
      ++it;
  }

  return no_change;
}

bool replace_expr(const replace_mapt &what, exprt &dest)
{
  bool no_change = true;

  for(auto it = dest.depth_begin(), itend = dest.depth_end();
      it != itend;) // No ++it
  {
    replace_mapt::const_iterator findit = what.find(*it);
    if(findit != what.end())
    {
      PRECONDITION_WITH_DIAGNOSTICS(
        it->type() == findit->second.type(),
        "types to be replaced should match. old type:\n" + it->type().pretty() +
          "\nnew type:\n" + findit->second.type().pretty());

      it.mutate() = findit->second;
      no_change = false;
      it.next_sibling_or_parent();
    }
    else
      ++it;
  }

  return no_change;
}

namespace
{
/// Replaces in each node of a DAG once, by the results for its nodes.
class shared_replacert
{
public:
  explicit shared_replacert(std::function<const exprt *(const exprt &)> find)
    : find(std::move(find))
  {
  }

  /// \return whether \p expr is unchanged
  bool operator()(exprt &expr)
  {
    exprt result = replaced(expr);
    if(&result.read() == &expr.read())
      return true;
    expr = std::move(result);
    return false;
  }

private:
  std::function<const exprt *(const exprt &)> find;
  /// Results by node, with the original kept alive so that its address is
  /// not reused.
  std::unordered_map<const void *, std::pair<exprt, exprt>> visited;

  exprt replaced(const exprt &expr)
  {
    const void *const node = &expr.read();
    const auto found = visited.find(node);
    if(found != visited.end())
      return found->second.second;
    exprt result = expr;
    if(const exprt *by = find(expr))
    {
      PRECONDITION_WITH_DIAGNOSTICS(
        expr.type() == by->type(),
        "types to be replaced should match. old type:\n" +
          expr.type().pretty() + "\nnew type:\n" + by->type().pretty());
      result = *by;
    }
    else
    {
      for(std::size_t i = 0; i < expr.operands().size(); ++i)
      {
        exprt op = replaced(expr.operands()[i]);
        if(&op.read() != &expr.operands()[i].read())
          result.operands()[i] = std::move(op);
      }
    }
    visited.emplace(node, std::make_pair(expr, result));
    return result;
  }
};
} // namespace

bool replace_expr_shared(const exprt &what, const exprt &by, exprt &dest)
{
  PRECONDITION_WITH_DIAGNOSTICS(
    what.type() == by.type(),
    "types to be replaced should match. old type:\n" + what.type().pretty() +
      "\nnew type:\n" + by.type().pretty());
  return shared_replacert{[&](const exprt &e) -> const exprt * {
    return e == what ? &by : nullptr;
  }}(dest);
}

bool replace_expr_shared(const replace_mapt &what, exprt &dest)
{
  return shared_replacert{
    [&](const exprt &e) -> const exprt *
    {
      const auto it = what.find(e);
      return it == what.end() ? nullptr : &it->second;
    }}(dest);
}
