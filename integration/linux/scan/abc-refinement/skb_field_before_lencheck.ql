/**
 * @name Field read from a received buffer before a length check
 * @description Gap #2 oracle (taint-generalised).  Finds the "read a
 *   multi-byte field from a received buffer before validating it is long
 *   enough" shape behind remote OOB reads in two buffer flavours:
 *   (a) skb->data parsers -- L2CAP (CVE-2026-31393/31512/31513), btmtk
 *       (CVE-2026-46140), virtio_bt (CVE-2026-46123/46186), ibmasm
 *       (CVE-2026-46064); read via a struct cast / get_unaligned_* /
 *       memcpy with no pskb_may_pull / `skb->len >= N` guard;
 *   (b) bounded-cursor parsers -- a `(void *p, void *end)` / `(buf, len)`
 *       function (ceph CVE-2026-43406/43407, rxrpc, XDR) that decodes a
 *       field from the cursor (ceph_decode_ / get_unaligned / ...) with no
 *       ceph_decode_need / pskb_may_pull / length guard.
 *   Function-granularity and deliberately recall-oriented (the guard may
 *   live in a caller) -- mirrors tlv_parse_loop_helper.ql; stage-2 (CBMC /
 *   manual) adjudicates.
 * @kind problem
 * @id cpp/abc/field-read-before-lencheck
 * @problem.severity warning
 */
import cpp
import Scope
import KernelTaint

/** A "structured" read of skb->data: cast to a pointer-to-struct, passed
 *  to get_unaligned_ / memcpy / skb_header_pointer, or dereferenced.
 *  These read 2+ bytes at an offset, which is where the OOB lands. */
predicate structuredSkbRead(KernelTaint::SkbDataAccess sda) {
  exists(Cast c |
    c.getExpr() = sda and
    c.getType().getUnspecifiedType() instanceof PointerType)
  or
  exists(FunctionCall fc |
    sda = fc.getAnArgument().getAChild*() and
    fc.getTarget().getName().matches(["get_unaligned%", "memcpy", "memmove",
      "%skb_header_pointer", "le16_to_cpu%", "be16_to_cpu%"]))
}

/** Trivial header accessor to exclude: `static inline struct X *foo_hdr
 *  (skb) { return (struct X *)skb->data; }` -- the caller owns the length
 *  check by design. */
predicate trivialAccessor(Function f, KernelTaint::SkbDataAccess sda) {
  f.getName().matches("%\\_hdr")
  or
  exists(ReturnStmt ret |
    ret.getEnclosingFunction() = f and
    sda = ret.getExpr().getAChild*() and
    f.getBlock().getNumStmt() <= 2)
}

/** advisory: the enclosing function has SOME length-guard call (existential,
 *  NOT proven to dominate this read -- advisory only, never a drop). */
string lenGuardAdvisory(Function f) {
  if KernelTaint::hasLengthGuard(f) then result = "GUARDED" else result = "UNGUARDED"
}

/** advisory: a trivial `*_hdr`-style accessor (caller owns the check by
 *  design) -- a recall heuristic, advisory only. */
string trivialAdvisory(Function f, KernelTaint::SkbDataAccess sda) {
  if trivialAccessor(f, sda) then result = "yes" else result = "no"
}

/** advisory: a length-ish parameter (len/size/max/count) is PROPAGATED as an
 *  argument to a callee -- the function delegates the bound downward (e.g.
 *  the IB MAD dispatcher subn_get_opa_sma passing max_len to
 *  __subn_get_opa_*).  Length-aware: the read is bounded by the callee, so
 *  the in-function hasLengthGuard miss is not evidence of a real OOB.
 *  Existential -> advisory. */
predicate lenParamPropagated(Function f) {
  exists(Parameter lp, FunctionCall fc |
    lp.getFunction() = f and
    lp.getUnspecifiedType() instanceof IntegralType and
    lp.getName()
        .toLowerCase()
        .matches(["%len%", "%size%", "%max%", "%count%"]) and
    fc.getEnclosingFunction() = f and
    fc.getAnArgument().(VariableAccess).getTarget() = lp)
}

string lenPropAdvisory(Function f) {
  if lenParamPropagated(f) then result = "LENPROP" else result = "no"
}

from Function f, int line, string kind, string adv
where
  inScope(f) and
  (
    // (a) skb->data parser -- emitted regardless of guard/trivial status;
    //     those become ADVISORY fields (sound over-approximate collector).
    exists(KernelTaint::SkbDataAccess sda |
      sda.getEnclosingFunction() = f and
      structuredSkbRead(sda) and
      line = sda.getLocation().getStartLine() and
      kind = "skb->data read" and
      adv = "adv_lenguard=" + lenGuardAdvisory(f) +
        "|adv_trivial=" + trivialAdvisory(f, sda) + "|adv_lenprop=" + lenPropAdvisory(f))
    or
    // (b) bounded-cursor parser: a (buf,len)/(p,end) function that decodes
    //     a field from the cursor
    exists(KernelTaint::DecodeCall dc |
      KernelTaint::isBoundedBufferParam(f, _) and
      dc.getEnclosingFunction() = f and
      line = dc.getLocation().getStartLine() and
      kind = "bounded-cursor decode" and
      adv = "adv_lenguard=" + lenGuardAdvisory(f) + "|adv_lenprop=" + lenPropAdvisory(f))
  )
select f,
  f.getName() + "|" + f.getFile().getAbsolutePath() + "|" + line.toString() +
  "|" + kind + "|impact=READ|" + adv
