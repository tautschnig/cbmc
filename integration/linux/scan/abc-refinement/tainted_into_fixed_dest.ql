/**
 * @name Tainted length into a fixed-size destination without a constant clamp
 * @description Sharpened bug-shape query.  Flags a user-controlled length
 *   reaching the size argument of a copy whose DESTINATION is a fixed-size
 *   array (field or variable), where the length is NOT clamped against a
 *   compile-time constant anywhere in the enclosing function.  This is the
 *   shape of the real findings (fixed buffer + unclamped user size) and
 *   excludes the dominant FP classes (proportional allocation -> dest is a
 *   malloc'd pointer, not an array; and clamp-to-constant -> guarded).
 * @kind problem
 * @id cpp/abc/tainted-into-fixed-dest
 * @problem.severity warning
 */
import cpp
import semmle.code.cpp.dataflow.new.TaintTracking
import semmle.code.cpp.dataflow.new.DataFlow

class CopyCall extends FunctionCall {
  CopyCall() {
    this.getTarget().getName() =
      ["copy_from_user", "memcpy", "memmove", "_copy_from_user"]
  }
}

module CfgImpl implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node s) {
    exists(Parameter p |
      p.getName() = ["count", "len", "size", "n", "nbytes", "length"] and
      p.getUnderlyingType() instanceof IntegralType and
      s.asExpr() = p.getAnAccess())
  }

  predicate isSink(DataFlow::Node s) {
    exists(CopyCall c | s.asExpr() = c.getArgument(2))
  }
}

module Flow = TaintTracking::Global<CfgImpl>;

/** The fixed-size array that the copy destination expression *is*
 *  (after stripping array-to-pointer decay).  We deliberately do NOT
 *  descend into children, so indexing arrays like `kbuf[type]` or
 *  `image[minor].field` are not mistaken for the destination buffer. */
ArrayType fixedDestArray(CopyCall c) {
  result = c.getArgument(0).getType().getUnspecifiedType() and
  exists(result.getSize())
}

/** Holds if `p` is compared against a compile-time constant in `f`
 *  (proxy for "clamped to a fixed bound" -> safe, excluded). */
predicate clampedToConstant(Parameter p, Function f) {
  exists(RelationalOperation rel |
    rel.getEnclosingFunction() = f and
    rel.getAnOperand() = p.getAnAccess() and
    (
      rel.getAnOperand() instanceof Literal or
      rel.getAnOperand() instanceof SizeofOperator or
      rel.getAnOperand() instanceof EnumConstantAccess
    ))
}

from DataFlow::Node source, DataFlow::Node sink, CopyCall c, Parameter p,
  ArrayType destArr
where
  Flow::flow(source, sink) and
  sink.asExpr() = c.getArgument(2) and
  source.asExpr() = p.getAnAccess() and
  destArr = fixedDestArray(c) and
  not clampedToConstant(p, c.getEnclosingFunction())
select sink,
  c.getEnclosingFunction().getName() + "|" +
  c.getFile().getAbsolutePath() + "|" +
  c.getLocation().getStartLine().toString() + "|" +
  c.getTarget().getName() + "|" + p.getName() + "|" +
  "destArr[" + destArr.getSize().toString() + "]"
