/*******************************************************************\

Module: Symbolic Execution Constant Propagation

Author: Michael Tautschig, tautschn@amazon.com

\*******************************************************************/

/// \file
/// GOTO Symex constant propagation

#ifndef CPROVER_GOTO_SYMEX_GOTO_SYMEX_CAN_FORWARD_PROPAGATE_H
#define CPROVER_GOTO_SYMEX_GOTO_SYMEX_CAN_FORWARD_PROPAGATE_H

#include <util/expr.h>
#include <util/expr_util.h>
#include <util/std_expr.h>

class goto_symex_can_forward_propagatet : public can_forward_propagatet
{
public:
  explicit goto_symex_can_forward_propagatet(const namespacet &ns)
    : can_forward_propagatet(ns)
  {
  }

protected:
  bool is_constant(const exprt &expr) const override
  {
    if(expr.id() == ID_mult)
    {
      bool found_non_constant = false;

      // propagate stuff with sizeof in it
      for(const auto &op : expr.operands())
      {
        if(op.find(ID_C_c_sizeof_type).is_not_nil())
          return true;
        else if(!is_constant(op))
          found_non_constant = true;
      }

      return !found_non_constant;
    }
    else if(expr.id() == ID_if)
    {
      // A value merged from branches, `guard ? c1 : c2` with constant
      // (or again merged) values: propagating it lets conditions such as
      // `x == c3` simplify to false, so that symbolic execution does not
      // explore branches that no path can take. The number of values is
      // bounded, as each merge may double them.
      std::size_t leaves = 0;
      return is_merged_constant(expr, leaves);
    }
    else if(expr.id() == ID_with)
    {
      // this is bad
#if 0
      for(const auto &op : expr.operands())
      {
        if(!is_constant(op))
          return false;
      }

      return true;
#else
      return false;
#endif
    }

    return can_forward_propagatet::is_constant(expr);
  }

private:
  /// Whether \p expr is a constant, or `c ? a : b` with \p a and \p b
  /// such, with at most 8 constants in total (counted in \p leaves).
  bool is_merged_constant(const exprt &expr, std::size_t &leaves) const
  {
    if(expr.id() != ID_if)
      return expr.is_constant() && ++leaves <= 8;
    const if_exprt &if_expr = to_if_expr(expr);
    return is_merged_constant(if_expr.true_case(), leaves) &&
           is_merged_constant(if_expr.false_case(), leaves);
  }
};

#endif // CPROVER_GOTO_SYMEX_GOTO_SYMEX_CAN_FORWARD_PROPAGATE_H
