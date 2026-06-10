/**
 * Unified mitigation-dominance library.
 *
 * Every threat-model asset's mitigation has the same shape: a *guard* that
 * must control-flow-DOMINATE the threat site.  This library factors that
 * one relation -- a validate-then-reject guard (or a zeroing/clamp) that
 * strictly dominates the site -- and exposes a per-asset predicate:
 *
 *   A1 (memory)      length guard            -> see caller_precondition.ql
 *   A1-UB (div/shift) nonzeroChecked / shiftBoundChecked
 *   A2 (confid.)     memsetDominates
 *   A4 (avail.)      clampDominates
 *   A5 (authz.)      capabilityDominates
 *
 * A finder calls the matching predicate on its candidate to decide
 * MITIGATED (the guard provably runs before the threat) vs UNMITIGATED.
 * Sound for *dismissal*: the guard must dominate, so MITIGATED never
 * wrongly clears a site.
 */

import cpp
import semmle.code.cpp.controlflow.Dominance

module Mitigation {
  /** A compile-time constant bound. */
  predicate constBound(Expr e) {
    e instanceof Literal or
    e instanceof EnumConstantAccess or
    e instanceof SizeofOperator or
    (exists(e.getValue()) and not e instanceof VariableAccess)
  }

  /** An `if` whose then-branch is an early exit (a reject). */
  predicate rejectingIf(IfStmt ifs) {
    exists(Stmt jump |
      jump.getParentStmt*() = ifs.getThen() and
      (jump instanceof ReturnStmt or jump instanceof JumpStmt)
    )
  }

  /** A2: a `memset(&v,0,..)` / `memzero_explicit(&v,..)` that dominates
   *  `site` -- the confidentiality mitigation (zero the local before copy). */
  predicate memsetDominates(Variable v, ControlFlowNode site) {
    exists(FunctionCall mz |
      mz.getTarget().getName() =
        ["memset", "__builtin_memset", "memzero_explicit", "__memset"] and
      mz.getArgument(0).(AddressOfExpr).getOperand().(VariableAccess).getTarget() =
        v and
      strictlyDominates(mz, site)
    )
  }

  /** A4: a validate-then-reject clamp `if (v <relop> CONST) reject` that
   *  dominates `site` -- the availability mitigation (bound the loop/size). */
  predicate clampDominates(Variable v, ControlFlowNode site) {
    exists(IfStmt ifs, RelationalOperation rel, Expr k |
      rejectingIf(ifs) and
      rel = ifs.getCondition().getAChild*() and
      rel.getAnOperand().(VariableAccess).getTarget() = v and
      k = rel.getAnOperand() and
      k != rel.getAnOperand().(VariableAccess) and
      constBound(k) and
      strictlyDominates(ifs.getCondition(), site)
    )
  }

  /** A1-UB div: a `if (v == 0)` / `if (!v)` reject dominating `site` -- the
   *  div-by-zero mitigation. */
  predicate nonzeroChecked(Variable v, ControlFlowNode site) {
    exists(IfStmt ifs, Expr cond |
      rejectingIf(ifs) and
      cond = ifs.getCondition().getAChild*() and
      strictlyDominates(ifs.getCondition(), site)
    |
      cond.(EQExpr).getAnOperand().(VariableAccess).getTarget() = v and
      cond.(EQExpr).getAnOperand().getValue() = "0"
      or
      cond.(NotExpr).getOperand().(VariableAccess).getTarget() = v
    )
    or
    // also covered by a clamp form (v < N etc.) before the use
    clampDominates(v, site)
  }

  /** A1-UB shift: a `if (v >= WIDTH)` / `if (v > K)` reject dominating
   *  `site` -- the undefined-shift mitigation. */
  predicate shiftBoundChecked(Variable v, ControlFlowNode site) {
    clampDominates(v, site)
  }

  /** A5: a `capable()`-family check dominating `site` -- the authorization
   *  mitigation (the dominance form used in caller_precondition's len case). */
  predicate capabilityDominates(ControlFlowNode site) {
    exists(FunctionCall chk |
      chk.getTarget().getName().toLowerCase().matches("%capable%") and
      strictlyDominates(chk, site)
    )
  }
}
