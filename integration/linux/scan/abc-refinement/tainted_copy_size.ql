/**
 * @name Tainted user length reaches a copy size (stage-1 over-approx)
 * @description Over-approximating taint: a user-controlled
 *   length parameter (the read/write fops `count`, or a
 *   `len`/`size` argument) reaches the size argument of
 *   copy_from_user / copy_to_user / memcpy / memmove.  Flags
 *   every reachable flow regardless of clamping, so it
 *   over-reports; CBMC (stage 2) refines each candidate.
 * @kind problem
 * @id cpp/abc/tainted-copy-size
 * @problem.severity warning
 */
import cpp
import semmle.code.cpp.dataflow.new.TaintTracking
import semmle.code.cpp.dataflow.new.DataFlow

class CopyCall extends FunctionCall {
  CopyCall() {
    this.getTarget().getName() =
      ["copy_from_user", "copy_to_user", "memcpy", "memmove", "_copy_from_user", "_copy_to_user"]
  }
}

module CfgImpl implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node s) {
    exists(Parameter p |
      p.getName() = ["count", "len", "size", "n", "nbytes"] and
      p.getUnderlyingType() instanceof IntegralType and
      s.asExpr() = p.getAnAccess())
  }

  predicate isSink(DataFlow::Node s) {
    exists(CopyCall c | s.asExpr() = c.getArgument(2))
  }
}

module Flow = TaintTracking::Global<CfgImpl>;

from DataFlow::Node source, DataFlow::Node sink, CopyCall c
where Flow::flow(source, sink) and sink.asExpr() = c.getArgument(2)
select sink,
  "stage-1: tainted length reaches " + c.getTarget().getName() + " size in "
    + sink.asExpr().getEnclosingFunction().getName()
    + " (" + sink.asExpr().getFile().getBaseName() + ":"
    + sink.asExpr().getLocation().getStartLine() + ")"
