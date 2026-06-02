/**
 * @name Tainted length reaches memcpy size (stage-1 over-approx)
 * @description Over-approximating taint: a value from the
 *   untrusted source reaches a memcpy size argument.  Flags
 *   every reachable flow regardless of guard sufficiency, so
 *   it over-reports; CBMC (stage 2) refines each candidate.
 * @kind problem
 * @id cpp/abc/tainted-memcpy-size
 * @problem.severity warning
 */
import cpp
import semmle.code.cpp.dataflow.new.TaintTracking
import semmle.code.cpp.dataflow.new.DataFlow

module CfgImpl implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node s) {
    s.asExpr().(FunctionCall).getTarget().hasName("untrusted_len")
  }

  predicate isSink(DataFlow::Node s) {
    exists(FunctionCall fc |
      fc.getTarget().hasName("memcpy") and
      s.asExpr() = fc.getArgument(2))
  }
}

module Flow = TaintTracking::Global<CfgImpl>;

from DataFlow::Node source, DataFlow::Node sink
where Flow::flow(source, sink)
select sink,
  "stage-1 candidate: tainted length reaches memcpy size in function "
    + sink.asExpr().getEnclosingFunction().getName()
