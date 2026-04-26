/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <util/byte_operators.h>
#include <util/invariant.h>
#include <util/simplify_expr.h>
#include <util/std_expr.h>

#include "boolbv.h"

literalt boolbvt::convert_equality(const equal_exprt &expr)
{
  const bool equality_types_match = expr.lhs().type() == expr.rhs().type();
  DATA_INVARIANT_WITH_DIAGNOSTICS(
    equality_types_match,
    "types of expressions on each side of equality should match",
    irep_pretty_diagnosticst{expr.lhs()},
    irep_pretty_diagnosticst{expr.rhs()});

  // see if it is an unbounded array
  if(is_unbounded_array(expr.lhs().type()))
  {
    // flatten byte_update/byte_extract operators if needed

    if(has_byte_operator(expr))
    {
      exprt simplified = simplify_expr(lower_byte_operators(expr, ns), ns);
      if(simplified.id() != ID_equal)
        return convert_bool(simplified);
      return record_array_equality(to_equal_expr(simplified));
    }

    return record_array_equality(expr);
  }

  // Simplify: (x + c1) == (x + c2) is false when c1 != c2.
  // Common in byte-addressed array stores (store at v+0, v+1, v+2, v+3).
  if(
    expr.lhs().id() == ID_plus && expr.rhs().id() == ID_plus &&
    to_plus_expr(expr.lhs()).operands().size() == 2 &&
    to_plus_expr(expr.rhs()).operands().size() == 2)
  {
    const auto &lp = to_plus_expr(expr.lhs());
    const auto &rp = to_plus_expr(expr.rhs());
    if(
      lp.op0() == rp.op0() && lp.op1().is_constant() &&
      rp.op1().is_constant() && lp.op1() != rp.op1())
      return const_literal(false);
    if(
      lp.op1() == rp.op1() && lp.op0().is_constant() &&
      rp.op0().is_constant() && lp.op0() != rp.op0())
      return const_literal(false);
  }

  const bvt &lhs_bv = convert_bv(expr.lhs());
  const bvt &rhs_bv = convert_bv(expr.rhs());

  DATA_INVARIANT_WITH_DIAGNOSTICS(
    lhs_bv.size() == rhs_bv.size(),
    "sizes of lhs and rhs bitvectors should match",
    irep_pretty_diagnosticst{expr.lhs()},
    "lhs size: " + std::to_string(lhs_bv.size()),
    irep_pretty_diagnosticst{expr.rhs()},
    "rhs size: " + std::to_string(rhs_bv.size()));

  if(lhs_bv.empty())
  {
    // An empty bit-vector comparison. It's not clear
    // what this is meant to say.
    return prop.new_variable();
  }

  return bv_utils.equal(lhs_bv, rhs_bv);
}

literalt boolbvt::convert_verilog_case_equality(
  const binary_relation_exprt &expr)
{
  // This is 4-valued comparison, i.e., z===z, x===x etc.
  // The result is always Boolean.

  DATA_INVARIANT_WITH_DIAGNOSTICS(
    expr.lhs().type() == expr.rhs().type(),
    "lhs and rhs types should match in verilog_case_equality",
    irep_pretty_diagnosticst{expr.lhs()},
    irep_pretty_diagnosticst{expr.rhs()});

  // Simplify: (x + c1) == (x + c2) is false when c1 != c2.
  // Common in byte-addressed array stores (store at v+0, v+1, v+2, v+3).
  if(
    expr.lhs().id() == ID_plus && expr.rhs().id() == ID_plus &&
    to_plus_expr(expr.lhs()).operands().size() == 2 &&
    to_plus_expr(expr.rhs()).operands().size() == 2)
  {
    const auto &lp = to_plus_expr(expr.lhs());
    const auto &rp = to_plus_expr(expr.rhs());
    if(
      lp.op0() == rp.op0() && lp.op1().is_constant() &&
      rp.op1().is_constant() && lp.op1() != rp.op1())
      return const_literal(false);
    if(
      lp.op1() == rp.op1() && lp.op0().is_constant() &&
      rp.op0().is_constant() && lp.op0() != rp.op0())
      return const_literal(false);
  }

  const bvt &lhs_bv = convert_bv(expr.lhs());
  const bvt &rhs_bv = convert_bv(expr.rhs());

  DATA_INVARIANT_WITH_DIAGNOSTICS(
    lhs_bv.size() == rhs_bv.size(),
    "bitvector arguments to verilog_case_equality should have the same size",
    irep_pretty_diagnosticst{expr.lhs()},
    "lhs size: " + std::to_string(lhs_bv.size()),
    irep_pretty_diagnosticst{expr.rhs()},
    "rhs size: " + std::to_string(rhs_bv.size()));

  if(expr.id()==ID_verilog_case_inequality)
    return !bv_utils.equal(lhs_bv, rhs_bv);
  else
    return bv_utils.equal(lhs_bv, rhs_bv);
}
