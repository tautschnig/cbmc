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

/** An expression subtree that mentions a compile-time bound. */
predicate hasConstBound(Expr e) {
  exists(Expr k |
    k = e.getAChild*() and
    (k instanceof Literal or k instanceof SizeofOperator or
     k instanceof EnumConstantAccess))
}

/** Holds if the copy's size argument is the result of a
 *  min()/min_t()/clamp() against a compile-time bound.  These kernel
 *  macros expand to a GNU statement-expression that hoists operands
 *  into temporaries, so the constant bound (e.g. `sizeof(buf)-1`)
 *  lives in a temp initializer inside the StmtExpr block — not in the
 *  ternary condition.  We therefore match: a StmtExpr whose value
 *  flows to the size argument and whose block contains a sizeof /
 *  literal / enum constant. */
predicate minClampedSink(CopyCall c) {
  exists(StmtExpr se |
    DataFlow::localExprFlow(se, c.getArgument(2)) and
    exists(Expr bound |
      (bound instanceof SizeofOperator or bound instanceof Literal or
       bound instanceof EnumConstantAccess) and
      bound.getEnclosingStmt().getParentStmt*() = se.getStmt()))
}

from DataFlow::Node source, DataFlow::Node sink, CopyCall c, Parameter p,
  ArrayType destArr
where
  Flow::flow(source, sink) and
  sink.asExpr() = c.getArgument(2) and
  source.asExpr() = p.getAnAccess() and
  destArr = fixedDestArray(c) and
  not clampedToConstant(p, c.getEnclosingFunction()) and
  not minClampedSink(c)
select sink,
  c.getEnclosingFunction().getName() + "|" +
  c.getFile().getAbsolutePath() + "|" +
  c.getLocation().getStartLine().toString() + "|" +
  c.getTarget().getName() + "|" + p.getName() + "|" +
  "destArr[" + destArr.getSize().toString() + "]"
