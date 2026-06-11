/**
 * @name A4 availability: interprocedural attacker-controlled allocation size
 * @description Interprocedural refinement of the A4 availability template.
 *   The intra-procedural finder (`av_unbounded.ql`) found 0 tainted alloc
 *   sizes because an allocation size is usually a length decoded in one
 *   function and passed across calls to the allocator in another.  This
 *   uses `TaintTracking::Global` from the shared raw sources
 *   (`KernelTaintFlow::isRawSourceNode`: skb->data / nla_data / ceph_decode
 *   / copy_from_user / byte-buffer params) to a kmalloc-family SIZE
 *   argument.
 *     asset   = availability (memory)
 *     actor   = attacker controlling a decoded length that flows to an alloc
 *     threat  = unbounded allocation (memory DoS) / overflow into undersize
 *     obligation (CBMC) = size <= K  (av_test.c)
 *   A MITIGATED verdict means a dominating clamp bounds the size variable.
 * @kind problem
 * @id cpp/abc/availability-alloc-interproc
 * @problem.severity warning
 */
import cpp
import semmle.code.cpp.dataflow.new.TaintTracking
import semmle.code.cpp.dataflow.new.DataFlow
import KernelTaintFlow
import MitigationDominance

/** A kmalloc-family allocation SIZE argument.  Matches both the plain
 *  names and the alloc-profiling `_noprof` variants the macros expand to
 *  in current kernels (e.g. `_kzalloc_noprof`, `_kmalloc_array_noprof`,
 *  `__kvmalloc_node_noprof`, `kmemdup_noprof`). */
predicate allocSizeArg(DataFlow::Node n, FunctionCall c) {
  c.getTarget()
      .getName()
      .matches([
          "kmalloc%", "kzalloc%", "kvmalloc%", "kvzalloc%", "vmalloc%",
          "vzalloc%", "kcalloc%", "sock_kmalloc%", "kmemdup%", "vmemdup%",
          "_kmalloc%", "_kzalloc%", "__kmalloc%", "__kvmalloc%", "krealloc%"
        ]) and
  n.asExpr() = c.getArgument([0 .. 1])
}

module AllocCfg implements DataFlow::ConfigSig {
  predicate isSource(DataFlow::Node n) { KernelTaintFlow::isRawSourceNode(n) }

  predicate isSink(DataFlow::Node n) { allocSizeArg(n, _) }
}

module AllocFlow = TaintTracking::Global<AllocCfg>;

/** Availability mitigation: a dominating clamp on the size variable. */
string avAllocVerdict(Expr sizeArg) {
  if
    exists(Variable v |
      sizeArg.(VariableAccess).getTarget() = v and
      Mitigation::clampDominates(v, sizeArg)
    )
  then result = "MITIGATED"
  else result = "UNMITIGATED"
}

/** advisory: the enclosing function is itself an allocator wrapper (the
 *  alloc-profiling forwarding chain _k*alloc_noprof / kvmalloc_node ...),
 *  i.e. infrastructure forwarding its own size arg, not a subsystem passing
 *  attacker input.  Name-based -> advisory, never a drop. */
string allocWrapperAdvisory(FunctionCall c) {
  if
    c.getEnclosingFunction()
        .getName()
        .matches(["%alloc%", "%malloc%", "kmemdup%", "krealloc%", "vmemdup%"])
  then result = "yes"
  else result = "no"
}

from DataFlow::Node src, DataFlow::Node snk, FunctionCall c
where
  AllocFlow::flow(src, snk) and
  allocSizeArg(snk, c)
select c,
  c.getEnclosingFunction().getName() + "|" +
    c.getLocation().getFile().getAbsolutePath() + "|" +
    c.getLocation().getStartLine().toString() +
    "|A4-availability-interproc: alloc '" + c.getTarget().getName() +
    "' size from attacker input (src " +
    src.getLocation().getFile().getBaseName() + ":" +
    src.getLocation().getStartLine().toString() + ")|mitigation=" +
    avAllocVerdict(snk.asExpr()) + "|adv_alloc_wrapper=" +
    allocWrapperAdvisory(c)
