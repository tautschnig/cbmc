/**
 * @name A1 non-memory UB: tainted divisor or shift amount
 * @description THREAT TEMPLATE for the non-memory sub-classes of A1
 *   (absence of UB): a division/modulo whose DIVISOR, or a shift whose
 *   SHIFT-AMOUNT, is attacker-controlled (raw-taint-reachable) and not a
 *   compile-time constant.
 *     asset   = well-definedness (A1)
 *     actor   = attacker controlling the operand (wire/netlink/skb decode)
 *     threat  = division by zero (-> oops / A4 DoS) or oversized/negative
 *               shift (UB -> correctness / A1)
 *     obligation (CBMC) = --div-by-zero-check / --undefined-shift-check
 *               (ready; demonstrated in ub_test.c)
 *   The actor gate is the SAME taint analysis the memory-safety family
 *   uses (`KernelTaintFlow::isRawTaintReachable`); only the finder is new.
 *   Recall-oriented: stage-2 CBMC adjudicates whether a guard already
 *   bounds the operand.
 * @kind problem
 * @id cpp/abc/tainted-ub-arith
 * @problem.severity warning
 */
import cpp
import KernelTaintFlow

/** The divisor of a `/` or `%`. */
predicate divisor(Expr e, string op) {
  exists(DivExpr d | e = d.getRightOperand() and op = "div-by-zero")
  or
  exists(RemExpr r | e = r.getRightOperand() and op = "mod-by-zero")
}

/** The shift amount of a `<<` or `>>`. */
predicate shiftAmount(Expr e, string op) {
  exists(LShiftExpr l | e = l.getRightOperand() and op = "undefined-shift(<<)")
  or
  exists(RShiftExpr r | e = r.getRightOperand() and op = "undefined-shift(>>)")
}

from Expr operand, string op
where
  (divisor(operand, op) or shiftAmount(operand, op)) and
  not operand instanceof Literal and
  not exists(operand.getValue()) and // not a compile-time constant
  KernelTaintFlow::isRawTaintReachable(operand)
select operand,
  operand.getEnclosingFunction().getName() + "|" +
    operand.getLocation().getFile().getAbsolutePath() + "|" +
    operand.getLocation().getStartLine().toString() + "|A1-UB:" + op +
    "|operand '" + operand.toString() + "' is attacker-controlled"
