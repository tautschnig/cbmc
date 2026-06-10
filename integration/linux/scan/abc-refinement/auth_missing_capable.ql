/**
 * @name A5 authorization: privileged action not dominated by a capability check
 * @description THREAT TEMPLATE for the authorization asset (A5).  A call to
 *   a capability-requiring privileged primitive that is NOT dominated by a
 *   `capable()`-family check in the enclosing function.
 *     asset   = authorization
 *     actor   = unprivileged task reaching the handler
 *     threat  = the privileged action runs without the capability held
 *     obligation (CBMC) = privileged action ==> capability held (auth_test.c)
 *   Reuses the SAME control-flow dominance relation as
 *   caller_precondition.ql -- there the guard bounds a length; here it is a
 *   capability check that must dominate the action.  Recall-oriented and
 *   seed-list-driven: the privileged-primitive set is extensible, and (like
 *   the single-level skb-pull check) the capability check may legitimately
 *   live in a caller -- stage-2 / interprocedural-depth adjudicates.  In
 *   netlink code the check is often at REGISTRATION (GENL_ADMIN_PERM)
 *   rather than inline; that registration-level variant is the net/-specific
 *   refinement to derive next.
 * @kind problem
 * @id cpp/abc/auth-missing-capable
 * @problem.severity warning
 */
import cpp
import semmle.code.cpp.controlflow.Dominance

/** A capability check (the authorization guard). */
predicate capabilityCheck(FunctionCall c) {
  c.getTarget().getName().toLowerCase().matches("%capable%")
}

/** A privileged, capability-requiring primitive (seed list; extensible). */
predicate privilegedSink(FunctionCall c, string what) {
  exists(string n | n = c.getTarget().getName() |
    n = "commit_creds" and what = n
    or
    n.matches("call_usermodehelper%") and what = "call_usermodehelper"
    or
    n = ["override_creds", "prepare_kernel_cred"] and what = n
    or
    n = ["dev_set_promiscuity", "dev_set_allmulti", "dev_set_mac_address"] and
    what = "netdev-priv-op"
    or
    n.matches("dev_change_flags%") and what = "dev_change_flags"
    or
    n = ["sched_setscheduler_nocheck", "set_user_nice"] and what = "sched-priv"
  )
}

from FunctionCall sink, Function f, string what
where
  privilegedSink(sink, what) and
  f = sink.getEnclosingFunction() and
  not exists(FunctionCall chk |
    capabilityCheck(chk) and
    chk.getEnclosingFunction() = f and
    strictlyDominates(chk, sink)
  )
select sink,
  f.getName() + "|" + sink.getLocation().getFile().getAbsolutePath() + "|" +
    sink.getLocation().getStartLine().toString() +
    "|A5 authorization: privileged action '" + what +
    "' not dominated by a capability check in this function"
