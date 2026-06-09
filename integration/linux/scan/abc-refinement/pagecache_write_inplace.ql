/**
 * @name Page-cache write via in-place crypto over user-iov-extracted pages
 * @description "Needle" oracle for the copyfail LPE class (the originating
 *   CVE of this effort).  splice/vmsplice can deliver references to
 *   READ-ONLY page-cache pages (e.g. a setuid binary) into a crypto
 *   request's scatterlist via a user iov_iter; an IN-PLACE cipher
 *   operation then WRITES the transform result back into those pages,
 *   yielding an arbitrary page-cache overwrite primitive.
 *
 *   The flagged shape: a function (a) obtains a scatterlist/pages from a
 *   user iov_iter via a page-extraction provenance call
 *   (extract_iter_to_sg / iov_iter_extract_pages / iov_iter_get_pages /
 *   af_alg_make_sg), AND (b) issues an in-place crypto write -- a
 *   *_request_set_crypt() whose source and destination scatterlist
 *   arguments are the SAME expression -- with NO intervening copy into a
 *   private buffer.  This is a provenance/aliasing oracle, orthogonal to
 *   the input-taint layer.
 *
 *   NOTE (honest scope): the af_alg instance behind copyfail is subtler --
 *   src and dst are distinct sgl objects that can ALIAS the same physical
 *   pages when user space reuses one buffer for send+recv; that aliasing
 *   is not syntactically visible here.  This oracle catches the common
 *   explicit in-place shape (src == dst) and is a recall-oriented starting
 *   point; stage-2 / manual review adjudicates the aliasing case.
 * @kind problem
 * @id cpp/abc/pagecache-write-inplace
 * @problem.severity warning
 */
import cpp

/** A call that extracts pages from a user iov_iter into a scatterlist --
 *  the provenance that may alias read-only page-cache pages. */
class IovPageExtraction extends FunctionCall {
  IovPageExtraction() {
    this.getTarget()
        .getName()
        .matches([
            "extract_iter_to_sg", "iov_iter_extract_pages",
            "iov_iter_get_pages%", "af_alg_make_sg", "netfs_extract_iter%"
          ])
  }
}

/** A crypto "set_crypt" call configuring source and destination sglists. */
class SetCryptCall extends FunctionCall {
  SetCryptCall() {
    this.getTarget().getName().matches("%_request_set_crypt")
  }

  Expr getSrcSgl() { result = this.getArgument(1) }

  Expr getDstSgl() { result = this.getArgument(2) }
}

/** Two expressions denoting the same scatterlist (same variable, or same
 *  field access chain) -- an explicit in-place (src == dst) crypto op. */
predicate sameSgl(Expr a, Expr b) {
  a.(VariableAccess).getTarget() = b.(VariableAccess).getTarget() and
  not a instanceof FieldAccess
  or
  a.(FieldAccess).getTarget() = b.(FieldAccess).getTarget() and
  sameSgl(a.(FieldAccess).getQualifier(), b.(FieldAccess).getQualifier())
}

/** A copy of the extracted data into a private buffer before the write --
 *  the canonical sanitizer (the fix copies out of the user pages). */
predicate hasPrivateCopy(Function f) {
  exists(FunctionCall fc | fc.getEnclosingFunction() = f |
    fc.getTarget()
        .getName()
        .matches([
            "sg_copy%", "memcpy_from_msg", "copy_from_iter%", "skcipher_walk%",
            "sg_copy_to_private", "%_copy_to_buffer"
          ])
  )
}

from Function f, IovPageExtraction ext, SetCryptCall sc
where
  ext.getEnclosingFunction() = f and
  sc.getEnclosingFunction() = f and
  sameSgl(sc.getSrcSgl(), sc.getDstSgl()) and
  not hasPrivateCopy(f)
select f,
  f.getName() + "|" + f.getFile().getAbsolutePath() + "|" +
    sc.getLocation().getStartLine().toString() +
    "|in-place crypto write (src==dst) over user-iov-extracted pages; " +
    "possible page-cache overwrite primitive"
