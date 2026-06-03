/**
 * @name User sizeimage reaches hmm_store size without BO bound check
 * @description Path query: a user-controlled `v4l2_*format.sizeimage`
 *   field reaches the `bytes` argument (arg 2) of `hmm_store`, whose
 *   destination buffer object is sized independently (by width/height).
 *   Demonstrates user-reachability of the unbounded hmm_store copy.
 * @kind path-problem
 * @id cpp/abc/atomisp-sizeimage-to-hmm-store
 * @problem.severity warning
 */
import cpp
import semmle.code.cpp.dataflow.new.TaintTracking
import semmle.code.cpp.dataflow.new.DataFlow

class HmmStoreCall extends FunctionCall {
  HmmStoreCall() { this.getTarget().getName() = "hmm_store" }
}

module CfgImpl implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node s) {
    exists(Parameter p |
      p.getName() = "arg" and
      s.asExpr() = p.getAnAccess())
  }

  predicate isSink(DataFlow::Node s) {
    exists(HmmStoreCall c | s.asExpr() = c.getArgument(2))
  }
}

module Flow = TaintTracking::Global<CfgImpl>;

import Flow::PathGraph

from Flow::PathNode source, Flow::PathNode sink
where Flow::flowPath(source, sink)
select sink.getNode(), source, sink,
  "user sizeimage reaches hmm_store size in "
    + sink.getNode().asExpr().getEnclosingFunction().getName()
