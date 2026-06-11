/**
 * @name Buffer-decoded length feeding round-up/align/multiply arithmetic
 * @description Gap #3 oracle.  Targets the generic byte-buffer parser
 *   family that the skb/TLV/count oracles miss: a length value DECODED
 *   FROM a received buffer (via ntohl/ntohs/be*_to_cpu/le*_to_cpu/
 *   get_unaligned_*) that is then fed into size arithmetic that can
 *   integer-overflow -- a round-up/align macro (xdr_round_up, round_up,
 *   roundup, ALIGN, DIV_ROUND_UP) or a multiplication -- whose result is
 *   used as a buffer/cursor advance or a bound.  This is the rxgk / XDR /
 *   ASN.1 / on-disk-structure shape behind e.g. CVE-2026-31633 (integer
 *   overflow in rxgk_verify_response: `xdr_round_up(resp_token_len)`
 *   wraps near UINT_MAX so the `> len` bound passes and the advance runs
 *   off the buffer).  Exactly the arithmetic CBMC's bounded reasoning is
 *   built to decide; pattern-matchers cannot.
 * @kind problem
 * @id cpp/abc/decoded-len-arith-overflow
 * @problem.severity warning
 */
import cpp
import Scope

/** A value decoded from a buffer: a byte-order / unaligned read.  Note
 *  ntohl/ntohs/be*_to_cpu are MACROS that bottom out in __builtin_bswap*
 *  / __swab* calls (or are no-ops on BE), so we match those plus the
 *  unaligned accessors. */
class DecodeCall extends FunctionCall {
  DecodeCall() {
    this.getTarget().getName()
        .matches(["__builtin_bswap%", "__swab16%", "__swab32%", "__swab64%",
                   "__fswab%", "get_unaligned%", "__get_unaligned%",
                   "be16_to_cpu%", "be32_to_cpu%", "be64_to_cpu%",
                   "le16_to_cpu%", "le32_to_cpu%", "le64_to_cpu%",
                   // subsystem decode accessors (taint sources): ceph and
                   // netlink read a length from the wire much like the
                   // byte-order builtins do.
                   "ceph_decode_8", "ceph_decode_16", "ceph_decode_32",
                   "ceph_decode_64", "nla_get_u8", "nla_get_u16",
                   "nla_get_u32", "nla_get_u64", "nla_get_be16",
                   "nla_get_be32", "nla_get_be64", "nla_get_le16",
                   "nla_get_le32", "nla_get_le64"])
  }
}

/** A local variable assigned the result of a decode call in `f`
 *  (the "length read from the wire"). */
predicate decodedLenVar(Function f, Variable v, DecodeCall dc) {
  dc.getEnclosingFunction() = f and
  (
    exists(AssignExpr a |
      a.getEnclosingFunction() = f and
      a.getLValue() = v.getAnAccess() and
      dc = a.getRValue().getAChild*())
    or
    v.getInitializer().getExpr().getAChild*() = dc and
    v.(LocalScopeVariable).getFunction() = f
  )
}

/** Round-up / align / round macros whose expansion can integer-overflow
 *  on a large input. */
predicate roundupMacroName(string n) {
  n = ["xdr_round_up", "round_up", "roundup", "ALIGN", "PAGE_ALIGN",
       "DIV_ROUND_UP", "xdr_object_len", "ALIGN_DOWN", "roundup_pow_of_two"]
}

/** The decoded length `v` feeds a round-up/align macro in `f`: a
 *  round-up macro invocation in `f` whose (unexpanded) argument text
 *  names `v`.  Using the unexpanded argument text is robust against the
 *  nested macro expansion (xdr_round_up -> round_up -> __round_mask). */
predicate feedsRoundup(Function f, Variable v, string detail, int line) {
  exists(MacroInvocation mi |
    roundupMacroName(mi.getMacro().getName()) and
    mi.getEnclosingFunction() = f and
    mi.getUnexpandedArgument(0).regexpMatch("(?s).*\\b" + v.getName() + "\\b.*") and
    detail = "decoded len '" + v.getName() + "' -> " + mi.getMacro().getName() + "()" and
    line = mi.getLocation().getStartLine())
}

/** The decoded length `v` is an operand of a multiplication in `f`
 *  (classic size = count * elem overflow).  We require `v` to be at least
 *  32-bit: a 16-/8-bit decoded value (e.g. ntohs -> u16) times a small
 *  constant cannot overflow the 32-bit arithmetic it is promoted to, so
 *  those are not real overflow candidates (e.g. IGMPv3 grec_nsrcs). */
predicate feedsMultiply(Function f, Variable v, string detail, int line) {
  v.getType().getSize() >= 4 and
  exists(MulExpr mul |
    mul.getEnclosingFunction() = f and
    mul.getAnOperand() = v.getAnAccess() and
    detail = "decoded len '" + v.getName() + "' -> multiply" and
    line = mul.getLocation().getStartLine())
}

from Function f, Variable v, DecodeCall dc, string detail, int line
where
  inScope(f) and
  decodedLenVar(f, v, dc) and
  (feedsRoundup(f, v, detail, line) or feedsMultiply(f, v, detail, line))
select f,
  f.getName() + "|" + f.getFile().getAbsolutePath() + "|" + line.toString() +
  "|" + detail
