/**
 * @name TLV/parse-loop with field-read or helper-based advance (recall ext.)
 * @description Recall extension of tlv_parse_loop.ql, at function
 *   granularity (the array-indexed version missed these two idioms):
 *   (A) direct field/array-read pointer advance — `cursor += hdr->len`
 *       (USB descriptor parsing); and
 *   (B) helper-based advance — a relational-bounded loop calling a helper
 *       that advances a pointer by a buffer-derived length
 *       (Bluetooth L2CAP `l2cap_get_conf_opt`).
 *   High-value remote/removable parse surfaces.  Stage-2 (CBMC) decides
 *   whether the per-iteration bound is sufficient.
 * @kind problem
 * @id cpp/abc/tlv-parse-loop-helper
 * @problem.severity warning
 */
import cpp

/** += / -=, integer or pointer flavour. */
class AdvanceOp extends AssignOperation {
  AdvanceOp() {
    this instanceof AssignAddExpr or this instanceof AssignSubExpr or
    this instanceof AssignPointerAddExpr or this instanceof AssignPointerSubExpr
  }
}

/** Advance amount read out of the parsed buffer (in-band length). */
predicate bufferDerivedAdvance(AdvanceOp adv) {
  exists(Expr len |
    len = adv.getRValue().getAChild*() and
    (len instanceof FieldAccess or len instanceof ArrayExpr))
}

/** A function that advances a pointer and reads a buffer field/array
 *  (the in-band length may flow through a local first, e.g. l2cap:
 *  `len = SIZE + opt->len; *ptr += len;`).  Recall-oriented; gated
 *  downstream by the caller having a relational-bounded loop. */
class AdvancingHelper extends Function {
  AdvancingHelper() {
    exists(AdvanceOp adv |
      adv.getEnclosingFunction() = this and
      (adv instanceof AssignPointerAddExpr or adv instanceof AssignPointerSubExpr)) and
    exists(Expr len |
      len.getEnclosingFunction() = this and
      (len instanceof FieldAccess or len instanceof ArrayExpr))
  }
}

/** A function with a loop bounded by a simple relational condition. */
predicate hasRelationalLoop(Function f) {
  exists(Loop l |
    l.getEnclosingFunction() = f and
    l.getControllingExpr() instanceof RelationalOperation)
}

from Function f, string kind
where
  hasRelationalLoop(f) and
  (
    // (A) the function itself advances a pointer by a buffer-read length
    exists(AdvanceOp adv |
      adv.getEnclosingFunction() = f and
      (adv instanceof AssignPointerAddExpr or adv instanceof AssignAddExpr) and
      bufferDerivedAdvance(adv)) and
    kind = "direct-field/array advance"
    or
    // (B) the function calls an advancing helper
    exists(FunctionCall fc, AdvancingHelper h |
      fc.getEnclosingFunction() = f and fc.getTarget() = h and h != f) and
    kind = "helper advance"
  )
select f,
  f.getName() + "|" + f.getFile().getAbsolutePath() + "|" +
  f.getLocation().getStartLine().toString() + "|" + kind
