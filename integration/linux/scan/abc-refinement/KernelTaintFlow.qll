/**
 * Kernel taint reachability (Phase 2+3 of the taint-source layer).
 *
 * Provides `isRawTaintReachable(Expr)` -- a SOUND, function-pointer-robust
 * "is this value derived from raw, unvalidated wire/netlink bytes?" signal
 * used to gate the count/index and TLV oracles.
 *
 * Design rationale (see taint-layer-status-2026-06.md): a single
 * `TaintTracking::Global` path-trace is NOT usable here because the
 * canonical true positive -- net/can/gw.c `cgw_csum_*`, whose array index
 * `crc8->result_idx` is bulk-filled from a raw netlink attribute by
 * `nla_memcpy()` -- reaches its use through an INDIRECT (function-pointer)
 * call `(*mod->csumfunc.crc8)(...)`, which breaks global dataflow.  So we
 * combine two cheap signals that together keep the TP and drop the
 * validated-param false positives (mac80211 `link[link_id]`):
 *
 *   (a) intra-procedural taint flow from a raw source to the value, and
 *   (b) the value is a field of a struct that is bulk-filled from raw
 *       wire/netlink bytes anywhere in the program (existential, so it is
 *       robust to the function-pointer indirection).
 *
 * Phase 3 sanitizer decision baked in: the RAW sources are `skb->data`,
 * `nla_data`/`nla_memcpy` (whole-payload, the policy only bounds `.len`),
 * `copy_from_user`, `ceph_decode_*`, and the unaligned/byte-order reads.
 * The TYPED `nla_get_*` accessors are deliberately NOT raw sources: they
 * are usually behind an NLA_POLICY range, so treating them as validated
 * is what drops the `link_id` cluster.
 */

import cpp
import KernelTaint
import semmle.code.cpp.dataflow.new.TaintTracking
import semmle.code.cpp.dataflow.new.DataFlow

module KernelTaintFlow {
  /** A call returning a raw, unvalidated wire/netlink payload pointer. */
  predicate isRawPayloadCall(FunctionCall fc) {
    fc.getTarget().getName() = ["nla_data", "nlmsg_data"]
  }

  /** A bulk copy of raw wire/netlink/user bytes into `dest`. */
  predicate isRawBulkCopy(FunctionCall fc, Expr dest) {
    fc.getTarget().getName() =
      ["nla_memcpy", "copy_from_user", "__copy_from_user", "memcpy_from_msg"] and
    dest = fc.getArgument(0)
  }

  /** The struct type behind a (possibly pointer) type. */
  Struct structOf(Type t) {
    result = t.getUnspecifiedType()
    or
    result = t.getUnspecifiedType().(PointerType).getBaseType().getUnspecifiedType()
    or
    result = t.getUnspecifiedType().(ArrayType).getBaseType().getUnspecifiedType()
  }

  /**
   * A struct type that, somewhere in the program, is bulk-filled from raw
   * wire/netlink bytes -- the destination of a raw bulk copy, or the
   * pointee of a cast applied to `skb->data` / `nla_data()`.  Fields of
   * such a struct that are used as array indices/counts are "raw-tainted
   * by association", robust to function-pointer call indirection.
   */
  predicate structCarriesRawBytes(Struct s) {
    // dest of nla_memcpy / copy_from_user etc.
    exists(FunctionCall fc, Expr dest |
      isRawBulkCopy(fc, dest) and s = structOf(dest.getType())
    )
    or
    // (struct s *)skb->data  or  (struct s *)nla_data(...)
    exists(Cast c |
      s = structOf(c.getType()) and
      (
        c.getExpr() instanceof KernelTaint::SkbDataAccess
        or
        isRawPayloadCall(c.getExpr())
      )
    )
  }

  /** A DataFlow source node for a raw wire/netlink/user read. */
  predicate isRawSourceNode(DataFlow::Node n) {
    n.asExpr() instanceof KernelTaint::DecodeCall
    or
    n.asExpr() instanceof KernelTaint::SkbDataAccess
    or
    isRawPayloadCall(n.asExpr())
    or
    exists(FunctionCall fc | isRawBulkCopy(fc, n.asDefiningArgument()))
    or
    // a read (subscript or deref) from a raw byte-buffer parameter --
    // e.g. b43 `desc[1]`, an RX-descriptor / packet cursor.  Restricted to
    // byte/void pointer PARAMETERS so it does not fire on validated struct
    // pointers (the `params->link_id` shape stays untainted).
    isByteBufferParamRead(n.asExpr())
  }

  /** A subscript/deref read whose base is a raw byte-buffer parameter. */
  predicate isByteBufferParamRead(Expr e) {
    exists(Parameter p, VariableAccess base |
      base.getTarget() = p and
      p.getUnspecifiedType() instanceof PointerType and
      (
        p.getUnspecifiedType().(PointerType).getBaseType().getUnspecifiedType()
          instanceof VoidType
        or
        p.getUnspecifiedType()
            .(PointerType)
            .getBaseType()
            .getUnspecifiedType()
            .(IntegralType)
            .getSize() = 1
      )
    |
      e.(ArrayExpr).getArrayBase() = base
      or
      e.(PointerDereferenceExpr).getOperand() = base
    )
  }

  /**
   * Holds if `e` is derived from raw, unvalidated wire/netlink bytes:
   * either by intra-procedural taint from a raw source (a), or by being a
   * field of a struct bulk-filled from raw bytes (b).
   */
  predicate isRawTaintReachable(Expr e) {
    // (a) intra-procedural taint flow to e
    exists(DataFlow::Node src, DataFlow::Node sink |
      isRawSourceNode(src) and
      sink.asExpr() = e and
      src.getEnclosingCallable() = sink.getEnclosingCallable() and
      TaintTracking::localTaint(src, sink)
    )
    or
    // (b) e is a field of a struct that carries raw bytes
    exists(FieldAccess fa | fa = e or e = fa.getAChild*() |
      structCarriesRawBytes(structOf(fa.getQualifier().getType()))
    )
  }
}
