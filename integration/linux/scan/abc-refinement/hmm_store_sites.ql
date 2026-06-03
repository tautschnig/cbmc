/**
 * @name hmm_store size argument provenance
 * @description Lists every hmm_store call with its enclosing function
 *   and size (arg 2) expression.  Used to pin the user-reachable
 *   site hmm_store(res->data, tmp_buf, arg->fmt.sizeimage); the
 *   ioctl->...->hmm_store reachability is established by call-graph
 *   tracing (see staging-triage-2026-06.md).
 * @kind problem
 * @id cpp/abc/hmm-store-size-provenance
 * @problem.severity warning
 */
import cpp

from FunctionCall c
where c.getTarget().getName() = "hmm_store"
select c,
  c.getEnclosingFunction().getName() + " (" +
  c.getFile().getBaseName() + ":" +
  c.getLocation().getStartLine().toString() + ") size=" +
  c.getArgument(2).toString()
