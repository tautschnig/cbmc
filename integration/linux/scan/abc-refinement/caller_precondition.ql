/**
 * @name Caller-precondition check for a bounded function-granularity hit
 * @description Automates the triage question behind the dominant residual
 *   false-positive class: a function flagged at FUNCTION granularity (it
 *   reads/uses a bound without an in-function check) may be perfectly safe
 *   because every CALLER validates the bound first.  The canonical case is
 *   rxkad_decrypt_ticket -- the flags byte is read with no in-function
 *   guard, but the caller rxkad_verify_response rejects ticket_len < 4.
 *
 *   For a function with a "bound" parameter (a len/size/count integral, or
 *   the size companion of a (ptr,size)/(p,end) cursor pair), this reports,
 *   per call site, whether the bound argument is constrained by a
 *   "validate-then-reject" guard in the caller -- an
 *   `if (V <relop> CONST) { return | goto | break }` that precedes the
 *   call.  Aggregated: callers / guarded-callers / verdict.  A
 *   CALLER-GUARDED verdict means the function-granularity oracle hit is an
 *   FP resolved by the callers.
 * @kind problem
 * @id cpp/abc/caller-precondition
 * @problem.severity warning
 */
import cpp

/** A "bound" parameter: a len/size/count-named integral parameter. */
predicate boundParam(Function f, Parameter bp) {
  bp.getFunction() = f and
  bp.getUnspecifiedType() instanceof IntegralType and
  bp.getName()
      .toLowerCase()
      .matches(["%len%", "%size%", "%count%", "%\\_nr", "nr\\_%", "%num%"])
}

/** A compile-time constant bound. */
predicate constBound(Expr e) {
  e instanceof Literal or
  e instanceof EnumConstantAccess or
  e instanceof SizeofOperator or
  (exists(e.getValue()) and not e instanceof VariableAccess)
}

/** Holds if `arg` (an actual argument at a call) is a variable that the
 *  caller constrains with a validate-then-reject guard before the call:
 *  `if (V <relop> CONST) { return|goto|break; }` located before `fc`. */
predicate guardedArg(FunctionCall fc, Expr arg) {
  exists(Variable v, IfStmt ifs, RelationalOperation rel, Expr k |
    arg.(VariableAccess).getTarget() = v and
    ifs.getEnclosingFunction() = fc.getEnclosingFunction() and
    rel = ifs.getCondition().getAChild*() and
    rel.getAnOperand().(VariableAccess).getTarget() = v and
    k = rel.getAnOperand() and
    k != rel.getAnOperand().(VariableAccess) and
    constBound(k) and
    // the then-branch is an early exit (reject)
    exists(Stmt jump |
      jump.getParentStmt*() = ifs.getThen() and
      (jump instanceof ReturnStmt or jump instanceof JumpStmt)
    ) and
    // guard precedes the call (line-order proxy for dominance)
    ifs.getLocation().getStartLine() < fc.getLocation().getStartLine() and
    ifs.getLocation().getFile() = fc.getLocation().getFile()
  )
}

/** Number of call sites of `target`. */
int numCallers(Function target) {
  result = count(FunctionCall fc | fc.getTarget() = target)
}

/** Number of call sites where the bound argument is caller-guarded. */
int numGuarded(Function target, Parameter bp) {
  result =
    count(FunctionCall fc |
      fc.getTarget() = target and
      guardedArg(fc, fc.getArgument(bp.getIndex()))
    )
}

/** Verdict string. */
bindingset[callers, guarded]
string verdict(int callers, int guarded) {
  if callers = guarded
  then result = "CALLER-GUARDED"
  else if guarded > 0 then result = "PARTIAL" else result = "UNGUARDED"
}

from Function target, Parameter bp, int callers, int guarded
where
  target.hasDefinition() and
  boundParam(target, bp) and
  callers = numCallers(target) and
  callers > 0 and
  guarded = numGuarded(target, bp)
select target,
  target.getName() + "|bound=" + bp.getName() + "|callers=" + callers +
    "|guarded=" + guarded + "|verdict=" + verdict(callers, guarded)
