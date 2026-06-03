/**
 * @name Tainted copy-size candidates — structured for auto-harnessing
 * @description Outputs structured facts for each tainted-size candidate:
 *   enclosing function, file path, sink line, sink function, source param.
 * @kind problem
 * @id cpp/abc/tainted-copy-size-structured
 * @problem.severity warning
 */
import cpp
import semmle.code.cpp.dataflow.new.TaintTracking
import semmle.code.cpp.dataflow.new.DataFlow

class CopyCall extends FunctionCall {
  CopyCall() {
    this.getTarget().getName() =
      ["copy_from_user", "copy_to_user", "memcpy", "memmove",
       "_copy_from_user", "_copy_to_user"]
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

from DataFlow::Node source, DataFlow::Node sink, CopyCall c, Parameter p
where
  Flow::flow(source, sink) and
  sink.asExpr() = c.getArgument(2) and
  source.asExpr() = p.getAnAccess()
select sink,
  sink.asExpr().getEnclosingFunction().getName() + "|" +
  sink.asExpr().getFile().getAbsolutePath() + "|" +
  sink.asExpr().getLocation().getStartLine().toString() + "|" +
  c.getTarget().getName() + "|" +
  p.getName() + "|" +
  c.getArgument(0).toString()
