/**
 * @name TLV / parse-loop advancing by a buffer-derived length
 * @description Finds the classic TLV/IE/option parser shape: a loop that
 *   advances an index/pointer by a length that was READ FROM the buffer
 *   being parsed.  This is the structure behind a large family of remote
 *   OOB reads (NFC LLCP, Bluetooth L2CAP options, 802.11 IEs, etc.):
 *   if the loop condition only checks `offset < total` (not
 *   `offset + header + value <= total`), an element near the end with a
 *   large embedded length walks the read/write past the buffer.  Stage-2
 *   (CBMC, loop unwinding) decides reachability with a concrete witness.
 * @kind problem
 * @id cpp/abc/tlv-parse-loop
 * @problem.severity warning
 */
import cpp

/** A loop whose advance step adds a buffer-read value to the cursor. */
class TlvLoop extends Loop {
  Variable cursor;     // the offset/pointer advanced each iteration
  Variable buf;        // the buffer being parsed and indexed

  TlvLoop() {
    // advance: cursor += <expr that reads from buf>   (cursor += 2 + len,
    // tlv += length + 2, etc.)  We look for an assign-add to `cursor`
    // whose RHS subtree contains an array/pointer read of `buf`, where the
    // same `buf` (or a value read from it) also feeds the added length.
    exists(AssignAddExpr adv |
      adv.getEnclosingStmt().getParentStmt*() = this.getStmt() and
      adv.getLValue() = cursor.getAnAccess()) and
    // a value READ FROM buf is used as a length in the loop body
    exists(ArrayExpr ae |
      ae.getEnclosingStmt().getParentStmt*() = this.getStmt() and
      ae.getArrayBase() = buf.getAnAccess()) and
    // the loop is bounded only by a simple `cursor < total` style condition
    this.getCondition() instanceof RelationalOperation and
    this.getControllingExpr().(RelationalOperation).getAnOperand() =
      cursor.getAnAccess()
  }

  Variable getCursor() { result = cursor }

  Variable getBuf() { result = buf }
}

/** Holds if the loop body assigns the cursor a value read from the buffer
 *  (i.e. the advance length is attacker-controlled, in-band). */
predicate advanceFromBuffer(TlvLoop loop) {
  exists(AssignAddExpr adv, ArrayExpr ae |
    adv.getEnclosingStmt().getParentStmt*() = loop.getStmt() and
    adv.getLValue() = loop.getCursor().getAnAccess() and
    ae = adv.getRValue().getAChild*() and
    ae.getArrayBase() = loop.getBuf().getAnAccess())
  or
  // common form: `len = buf[i+1]; ... cursor += K + len;`
  exists(AssignExpr la, ArrayExpr ae, Variable len, AssignAddExpr adv |
    la.getEnclosingStmt().getParentStmt*() = loop.getStmt() and
    la.getLValue() = len.getAnAccess() and
    ae = la.getRValue().getAChild*() and
    ae.getArrayBase() = loop.getBuf().getAnAccess() and
    adv.getEnclosingStmt().getParentStmt*() = loop.getStmt() and
    adv.getLValue() = loop.getCursor().getAnAccess() and
    adv.getRValue().getAChild*() = len.getAnAccess())
}

from TlvLoop loop
where advanceFromBuffer(loop)
select loop,
  loop.getEnclosingFunction().getName() + "|" +
  loop.getFile().getAbsolutePath() + "|" +
  loop.getLocation().getStartLine().toString() + "|" +
  "cursor=" + loop.getCursor().getName() + "|buf=" + loop.getBuf().getName()
