/**
 * @name A4 availability: attacker-controlled unbounded loop or allocation
 * @description THREAT TEMPLATE for the availability asset (A4): a loop
 *   BOUND, or an allocation SIZE, that is attacker-controlled
 *   (raw-taint-reachable) and not a compile-time constant.
 *     asset   = availability (bounded work / resource on attacker input)
 *     actor   = attacker controlling the loop bound / allocation size
 *     threat  = unbounded loop (CPU DoS) or unbounded allocation (memory DoS)
 *     obligation (CBMC) = loop terminates within K (--unwinding-assertions)
 *               / allocation size <= K  (av_test.c)
 *   Same actor gate as the rest (`KernelTaintFlow::isRawTaintReachable`).
 *   These sites OVERLAP the memory-safety oracles (a tainted loop bound is
 *   also an OOB risk; a tainted alloc size an overflow risk) -- but the
 *   AVAILABILITY obligation is different (termination / size<=K, not bounds
 *   / no-overflow), which is the threat-model point.  Recall-oriented;
 *   stage-2 CBMC adjudicates whether a clamp already bounds it.
 * @kind problem
 * @id cpp/abc/availability-unbounded
 * @problem.severity warning
 */
import cpp
import KernelTaintFlow

/** A loop bound or an allocation size expression. */
predicate availabilitySink(Expr e, string kind) {
  exists(Loop l, RelationalOperation rel |
    l.getControllingExpr() = rel and e = rel.getAnOperand() and
    kind = "unbounded-loop"
  )
  or
  exists(FunctionCall c |
    c.getTarget()
        .getName()
        .matches([
            "kmalloc", "kzalloc", "kvmalloc", "vmalloc", "kvzalloc",
            "kmalloc_array", "kcalloc", "kvmalloc_array", "kvcalloc",
            "sock_kmalloc", "kmemdup", "vmemdup_user", "memdup_user"
          ]) and
    e = c.getArgument([0 .. 1]) and
    kind = "unbounded-alloc"
  )
}

from Expr e, string kind
where
  availabilitySink(e, kind) and
  not e instanceof Literal and
  not exists(e.getValue()) and
  KernelTaintFlow::isRawTaintReachable(e)
select e,
  e.getEnclosingFunction().getName() + "|" +
    e.getLocation().getFile().getAbsolutePath() + "|" +
    e.getLocation().getStartLine().toString() + "|A4-availability:" + kind +
    "|attacker-controlled '" + e.toString() + "'"
