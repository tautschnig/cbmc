/**
 * @name Confidentiality threat: kernel->user copy of an unzeroed local
 * @description THREAT TEMPLATE (confidentiality asset).  Derived top-down
 *   from the threat model rather than from a prior CVE shape:
 *     asset    = confidentiality of kernel memory (non-public data)
 *     actor    = unprivileged user / remote peer receiving the copy
 *     threat   = a byte the actor receives carries leftover kernel data
 *                (uninitialised field or struct padding) -- an info leak
 *     obligation (for CBMC) = every byte of the copied region is initialised
 *   This finder flags the candidates: a LOCAL (stack) struct/array whose
 *   address is handed to a kernel->user/wire copy sink (copy_to_user,
 *   nla_put, skb_put_data, ...) with NO zeroing memset of that local in the
 *   function.  Struct padding and unset fields then leak.  Stage-2 CBMC
 *   discharges the obligation (see infoleak_test.c).  This is the
 *   confidentiality sibling of the memory-safety oracle family.
 * @kind problem
 * @id cpp/abc/infoleak-uninit-to-user
 * @problem.severity warning
 */
import cpp
import MitigationDominance

/** Mitigation verdict for a confidentiality candidate: MITIGATED when a
 *  zeroing memset of the local dominates the copy. */
string ilVerdict(Variable v, ExfilCall c) {
  if Mitigation::memsetDominates(v, c)
  then result = "MITIGATED"
  else result = "UNMITIGATED"
}

/** A kernel->user / kernel->wire copy sink and its kernel-source argument. */
class ExfilCall extends FunctionCall {
  Expr source;

  ExfilCall() {
    exists(string n | n = this.getTarget().getName() |
      n = ["copy_to_user", "_copy_to_user", "__copy_to_user"] and
      source = this.getArgument(1)
      or
      n.matches("nla_put%") and not n.matches("nla_put_u%") and
      source = this.getArgument(3)
      or
      n = "skb_put_data" and source = this.getArgument(1)
    )
  }

  Expr getSource() { result = source }
}

/** A local (stack) struct/array variable that is the copy source -- either
 *  `&v` or `v` (array decays). */
predicate localAggregateSource(ExfilCall c, LocalVariable v) {
  (
    c.getSource().(AddressOfExpr).getOperand().(VariableAccess).getTarget() = v
    or
    c.getSource().(VariableAccess).getTarget() = v
  ) and
  (
    v.getType().getUnspecifiedType() instanceof Struct or
    v.getType().getUnspecifiedType() instanceof ArrayType
  )
}

from Function f, ExfilCall c, LocalVariable v
where
  c.getEnclosingFunction() = f and
  localAggregateSource(c, v)
select c,
  f.getName() + "|" + f.getFile().getAbsolutePath() + "|" +
    c.getLocation().getStartLine().toString() +
    "|confidentiality: local '" + v.getName() + "' (" +
    v.getType().getUnspecifiedType().toString() +
    ") copied to user/wire|mitigation=" + ilVerdict(v, c)
