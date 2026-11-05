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
      // Propagating arbitrary updates (whose operands may be large or
      // symbolic) is bad; constant updates of constant arrays are not.
      return is_constant_array_update(expr);
    }

    return can_forward_propagatet::is_constant(expr);
  }

private:
  /// The maximum number of updates of a constant array value (see
  /// is_constant_array_update).
  static constexpr std::size_t max_array_updates = 64;

  /// Whether \p expr is an array value that updates a constant array (such
  /// as `array_of(0)`) at constant indices with constant values, at most
  /// max_array_updates times, e.g. the contents of a buffer after a copy
  /// of constant data into it. Reads at constant indices then simplify to
  /// constants. The bound keeps the expressions small: a value that would
  /// exceed it is not propagated, and later updates start a new chain.
  bool is_constant_array_update(const exprt &expr) const
  {
    std::size_t updates = 0;
    const exprt *value = &expr;
    while(value->id() == ID_with)
    {
      const with_exprt &with = to_with_expr(*value);
      if(
        with.type().id() != ID_array || ++updates > max_array_updates ||
        with.operands().size() != 3 || !is_constant(with.where()) ||
        !is_constant(with.new_value()))
      {
        return false;
      }
      value = &with.old();
    }
    return is_constant(*value);
  }

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
