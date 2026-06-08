/**
 * @name Field read from skb->data before a length check
 * @description Gap #2 oracle.  Finds the "read a multi-byte field from a
 *   received buffer before validating that the buffer is long enough"
 *   shape behind a recent cluster of remote OOB reads: L2CAP
 *   (CVE-2026-31393/31512/31513), btmtk (CVE-2026-46140), virtio_bt
 *   (CVE-2026-46123/46186), ibmasm (CVE-2026-46064), etc.  The function
 *   reads `skb->data` via a struct cast, get_unaligned_*, or memcpy
 *   source, but contains NO length guard (pskb_may_pull / skb_may_pull /
 *   an `skb->len >= N` comparison) at all.  Function-granularity and
 *   deliberately recall-oriented (the guard may legitimately live in a
 *   caller) -- mirrors tlv_parse_loop_helper.ql; stage-2 (CBMC / manual)
 *   adjudicates.  The fixed code pattern is exactly:
 *     if (!pskb_may_pull(skb, SIZE)) goto failed;
 *     hdr = (struct foo *)skb->data;   // or get_unaligned_le16(skb->data)
 * @kind problem
 * @id cpp/abc/field-read-before-lencheck
 * @problem.severity warning
 */
import cpp

/** A read of the `data` member of an sk_buff (`skb->data`). */
class SkbDataAccess extends FieldAccess {
  SkbDataAccess() {
    this.getTarget().getName() = "data" and
    this.getQualifier().getType().getUnspecifiedType().(PointerType)
        .getBaseType().getUnspecifiedType().(Struct).getName() = "sk_buff"
  }
}

/** A "structured" read of skb->data: cast to a pointer-to-struct, passed
 *  to get_unaligned_ / memcpy / skb_header_pointer, or dereferenced.
 *  These read 2+ bytes at an offset, which is where the OOB lands. */
predicate structuredSkbRead(SkbDataAccess sda) {
  // (struct foo *)skb->data  — cast to a pointer type
  exists(Cast c |
    c.getExpr() = sda and
    c.getType().getUnspecifiedType() instanceof PointerType)
  or
  // get_unaligned_le16(skb->data) / memcpy(dst, skb->data, n) / header_pointer
  exists(FunctionCall fc |
    sda = fc.getAnArgument().getAChild*() and
    fc.getTarget().getName().matches(["get_unaligned%", "memcpy", "memmove",
      "%skb_header_pointer", "le16_to_cpu%", "be16_to_cpu%"]))
}

/** Holds if function `f` contains ANY length guard on the received
 *  buffer: a pull/length-check call, or a relational op mentioning a
 *  `len`/`size` field/var. */
predicate hasLengthGuard(Function f) {
  exists(FunctionCall fc |
    fc.getEnclosingFunction() = f and
    fc.getTarget().getName().matches(["pskb_may_pull", "skb_may_pull",
      "__pskb_pull%", "pskb_pull", "skb_pull", "pskb_network_may_pull",
      "skb_header_pointer", "__skb_header_pointer"]))
  or
  exists(RelationalOperation rel, Expr op |
    rel.getEnclosingFunction() = f and
    op = rel.getAnOperand() and
    (
      op.(FieldAccess).getTarget().getName().toLowerCase().matches(["%len%", "%size%"])
      or
      op.(VariableAccess).getTarget().getName().toLowerCase().matches(["%len%", "%size%"])
    ))
}

from Function f, SkbDataAccess sda
where
  sda.getEnclosingFunction() = f and
  structuredSkbRead(sda) and
  not hasLengthGuard(f) and
  // drop trivial header accessors: `static inline struct X *foo_hdr(skb)
  // { return (struct X *)skb->data; }` -- the length check is the caller's
  // responsibility by design.  These are named `*_hdr` and just return the
  // cast.
  not f.getName().matches("%\\_hdr") and
  not exists(ReturnStmt ret |
    ret.getEnclosingFunction() = f and
    sda = ret.getExpr().getAChild*() and
    f.getBlock().getNumStmt() <= 2)
select f,
  f.getName() + "|" + f.getFile().getAbsolutePath() + "|" +
  sda.getLocation().getStartLine().toString() +
  "|skb->data read, no length guard in function"
