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

/** A `(p, end)` / `(p, limit)` cursor function: a pointer cursor parameter
 *  plus an `end`/`limit` pointer parameter (ceph / XDR / rxrpc style). */
predicate cursorFunction(Function f) {
  exists(Parameter p, Parameter e |
    p.getFunction() = f and e.getFunction() = f and p != e and
    p.getUnspecifiedType() instanceof PointerType and
    e.getUnspecifiedType() instanceof PointerType and
    e.getName().toLowerCase().matches(["%end%", "%limit%"])
  )
}

/** A caller establishes a byte-availability bound before the call: a
 *  ceph_decode_need / ceph_has_room / pskb_may_pull / *_safe check, or a
 *  relational comparison mentioning an `end`/`limit` cursor, located before
 *  the call.  Necessary-not-sufficient: it bounds the FIRST reads, not an
 *  arbitrary multi-field sub-decode. */
predicate cursorCallerGuard(FunctionCall fc) {
  exists(FunctionCall g |
    g.getEnclosingFunction() = fc.getEnclosingFunction() and
    g.getTarget()
        .getName()
        .matches([
            "ceph_decode_need", "ceph_has_room", "pskb_may_pull",
            "skb_header_pointer", "%\\_safe"
          ]) and
    g.getLocation().getStartLine() < fc.getLocation().getStartLine() and
    g.getLocation().getFile() = fc.getLocation().getFile()
  )
  or
  exists(RelationalOperation rel |
    rel.getEnclosingFunction() = fc.getEnclosingFunction() and
    rel.getAnOperand()
        .(VariableAccess)
        .getTarget()
        .getName()
        .toLowerCase()
        .matches(["%end%", "%limit%"]) and
    rel.getLocation().getStartLine() < fc.getLocation().getStartLine() and
    rel.getLocation().getFile() = fc.getLocation().getFile()
  )
}

/** Number of call sites where the cursor bound is established by the caller. */
int numGuardedCursor(Function target) {
  result =
    count(FunctionCall fc |
      fc.getTarget() = target and cursorCallerGuard(fc)
    )
}

/** Verdict string. */
bindingset[callers, guarded]
string verdict(int callers, int guarded) {
  if callers = guarded
  then result = "CALLER-GUARDED"
  else if guarded > 0 then result = "PARTIAL" else result = "UNGUARDED"
}

from Function target, string kind, int callers, int guarded, string bound
where
  target.hasDefinition() and
  callers = numCallers(target) and
  callers > 0 and
  (
    exists(Parameter bp |
      boundParam(target, bp) and
      kind = "len-param" and
      bound = bp.getName() and
      guarded = numGuarded(target, bp)
    )
    or
    // cursor functions WITHOUT a scalar len param (those are covered above)
    cursorFunction(target) and
    not exists(Parameter bp | boundParam(target, bp)) and
    kind = "cursor" and
    bound = "p,end" and
    guarded = numGuardedCursor(target)
  )
select target,
  target.getName() + "|kind=" + kind + "|bound=" + bound + "|callers=" +
    callers + "|guarded=" + guarded + "|verdict=" + verdict(callers, guarded)
