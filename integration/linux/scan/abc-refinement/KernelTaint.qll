/**
 * Kernel taint-source layer (shared library).
 *
 * Defines the kernel's attacker-controlled entry points (taint sources),
 * the bounded-buffer "cursor" parser shape, and the validators
 * (sanitizers) that the ABC oracles use to lift recall and precision.
 * See integration/linux/doc/taint-source-layer-plan-2026-06.md.
 *
 * This is intentionally a small, AST-level, tiered set of predicates --
 * not a full DataFlow configuration -- so the oracles can consume it
 * cheaply and incrementally.  Heavier interprocedural taint tracking can
 * be layered on top later (Phase 2+).
 */

import cpp

module KernelTaint {
  /**
   * Tier 1 -- wire/user decode accessors.  A call to one of these reads a
   * value "from the wire": byte-order/unaligned builtins (to which
   * ntohl/ntohs etc. lower), plus the ceph and netlink decode accessors.
   */
  predicate isDecodeAccessor(Function f) {
    f.getName()
        .matches([
            // byte-order / unaligned reads (ntohl/ntohs lower to these)
            "__builtin_bswap%", "__swab16%", "__swab32%", "__swab64%",
            "__fswab%", "get_unaligned%", "__get_unaligned%",
            "be16_to_cpu%", "be32_to_cpu%", "be64_to_cpu%",
            "le16_to_cpu%", "le32_to_cpu%", "le64_to_cpu%",
            // ceph cursor decoders
            "ceph_decode_8", "ceph_decode_16", "ceph_decode_32",
            "ceph_decode_64",
            // netlink attribute accessors
            "nla_get_u8", "nla_get_u16", "nla_get_u32", "nla_get_u64",
            "nla_get_be16", "nla_get_be32", "nla_get_be64", "nla_get_le16",
            "nla_get_le32", "nla_get_le64"
          ])
  }

  /** A call expression that decodes a value from the wire. */
  class DecodeCall extends FunctionCall {
    DecodeCall() { isDecodeAccessor(this.getTarget()) }
  }

  /** A read of the `data` member of an sk_buff (`skb->data`). */
  class SkbDataAccess extends FieldAccess {
    SkbDataAccess() {
      this.getTarget().getName() = "data" and
      this.getQualifier()
          .getType()
          .getUnspecifiedType()
          .(PointerType)
          .getBaseType()
          .getUnspecifiedType()
          .(Struct)
          .getName() = "sk_buff"
    }
  }

  /** A read of `bh->b_data` -- the on-disk bytes behind a `buffer_head`.
   *  The attacker controls the mounted image, so this is the filesystem
   *  analogue of `skb->data` (covers ext4 and every block filesystem). */
  class BufferHeadDataAccess extends FieldAccess {
    BufferHeadDataAccess() {
      this.getTarget().getName() = "b_data" and
      this.getQualifier()
          .getType()
          .getUnspecifiedType()
          .(PointerType)
          .getBaseType()
          .getUnspecifiedType()
          .(Struct)
          .getName() = "buffer_head"
    }
  }

  /** A call returning an attacker-controlled user buffer -- the generic
   *  user->kernel input across all syscall/ioctl paths (any subsystem). */
  predicate isUserInputCall(FunctionCall fc) {
    fc.getTarget()
        .getName()
        .matches([
            "memdup_user%", "vmemdup_user%", "strndup_user", "kmemdup_nul",
            "memdup_sockptr%", "copy_from_sockptr%"
          ])
  }

  /** A read of `fw->data` -- request_firmware() payload (attacker / vendor
   *  controlled), covering driver subsystems that load firmware/EEPROM. */
  class FirmwareDataAccess extends FieldAccess {
    FirmwareDataAccess() {
      this.getTarget().getName() = "data" and
      this.getQualifier()
          .getType()
          .getUnspecifiedType()
          .(PointerType)
          .getBaseType()
          .getUnspecifiedType()
          .(Struct)
          .getName() = "firmware"
    }
  }

  /** A read of `urb->transfer_buffer` -- USB transfer payload (a malicious
   *  / compromised device controls it), covering USB driver subsystems. */
  class UrbBufferAccess extends FieldAccess {
    UrbBufferAccess() {
      this.getTarget().getName() = "transfer_buffer" and
      this.getQualifier()
          .getType()
          .getUnspecifiedType()
          .(PointerType)
          .getBaseType()
          .getUnspecifiedType()
          .(Struct)
          .getName() = "urb"
    }
  }

  /** A read of `hid_field->value` -- the parsed HID report values (a
   *  malicious HID device controls the report), covering the HID stack. */
  class HidFieldValueAccess extends FieldAccess {
    HidFieldValueAccess() {
      this.getTarget().getName() = "value" and
      this.getQualifier()
          .getType()
          .getUnspecifiedType()
          .(PointerType)
          .getBaseType()
          .getUnspecifiedType()
          .(Struct)
          .getName() = "hid_field"
    }
  }

  /**
   * A function parameter that is a received-buffer cursor: a pointer
   * (void, u8 or char pointer) accompanied by a companion "bound"
   * parameter -- a len/size integral, or an end/limit pointer -- in the
   * same function.  This is the (void p, void end) / (buf, len) shape of
   * the kernel's bounded-cursor parsers (ceph, rxrpc, XDR, on-disk).
   */
  predicate isBoundedBufferParam(Function f, Parameter buf) {
    buf.getFunction() = f and
    buf.getUnspecifiedType() instanceof PointerType and
    // pointee is a byte-ish type (void/char/u8) -- a raw buffer cursor
    exists(Type pointee |
      pointee =
        buf.getUnspecifiedType().(PointerType).getBaseType().getUnspecifiedType()
    |
      pointee instanceof VoidType or
      pointee.(IntegralType).getSize() = 1
    ) and
    // a companion bound parameter
    exists(Parameter bound | bound.getFunction() = f and bound != buf |
      // length/size scalar
      bound.getUnspecifiedType() instanceof IntegralType and
      bound.getName().toLowerCase().matches(["%len%", "%size%", "%count%"])
      or
      // end/limit pointer
      bound.getUnspecifiedType() instanceof PointerType and
      bound.getName().toLowerCase().matches(["%end%", "%limit%"])
    )
  }

  /**
   * A length-guard / sanitizer call: validates that the buffer has enough
   * bytes before a read.  Presence of one of these in a function is taken
   * as evidence the parser bounds-checks its cursor.
   */
  predicate isLengthGuardCall(FunctionCall fc) {
    fc.getTarget()
        .getName()
        .matches([
            "pskb_may_pull", "skb_may_pull", "__pskb_pull%", "pskb_pull",
            "skb_pull", "pskb_network_may_pull", "skb_header_pointer",
            "__skb_header_pointer",
            // ceph cursor bounds checks (ceph_decode_need / *_safe macros
            // expand to a ceph_has_room() call)
            "ceph_decode_need", "ceph_decode_%_safe", "ceph_has_room",
            // netlink validation
            "nla_validate%", "nla_parse%", "nlmsg_parse%"
          ])
  }

  /**
   * A relational length guard: a comparison of a `len`/`size`-named scalar
   * or an `end`/`limit` pointer against something, anywhere in `f`.  Used
   * as a coarse "this function checks a length" signal.
   */
  predicate hasRelationalLengthGuard(Function f) {
    exists(RelationalOperation rel, Expr op |
      rel.getEnclosingFunction() = f and op = rel.getAnOperand()
    |
      op.(FieldAccess).getTarget().getName().toLowerCase().matches(["%len%", "%size%"])
      or
      op.(VariableAccess).getTarget().getName().toLowerCase().matches(["%len%", "%size%", "%end%", "%limit%"])
    )
  }

  /** Any length guard (call or relational) is present in `f`. */
  predicate hasLengthGuard(Function f) {
    exists(FunctionCall fc | fc.getEnclosingFunction() = f | isLengthGuardCall(fc))
    or
    hasRelationalLengthGuard(f)
  }
}
