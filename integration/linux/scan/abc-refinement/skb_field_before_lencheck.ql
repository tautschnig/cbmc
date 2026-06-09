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

from Function f, int line, string kind
where
  not KernelTaint::hasLengthGuard(f) and
  (
    // (a) skb->data parser
    exists(KernelTaint::SkbDataAccess sda |
      sda.getEnclosingFunction() = f and
      structuredSkbRead(sda) and
      not trivialAccessor(f, sda) and
      line = sda.getLocation().getStartLine() and
      kind = "skb->data read, no length guard")
    or
    // (b) bounded-cursor parser: a (buf,len)/(p,end) function that decodes
    //     a field from the cursor with no guard
    exists(KernelTaint::DecodeCall dc |
      KernelTaint::isBoundedBufferParam(f, _) and
      dc.getEnclosingFunction() = f and
      line = dc.getLocation().getStartLine() and
      kind = "bounded-cursor decode, no length guard")
  )
select f,
  f.getName() + "|" + f.getFile().getAbsolutePath() + "|" + line.toString() +
  "|" + kind
