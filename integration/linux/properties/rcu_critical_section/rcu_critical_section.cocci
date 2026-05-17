// @@
//   SmPL rule: rcu_critical_section.cocci
//
//   Coccinelle prefilter for the rcu_critical_section property
//   module.  Three call-site rules surface candidate sites:
//
//   1. `synchronize_rcu()` — must run outside RCU read-side
//      critical sections.  CBMC checks that the depth ghost is
//      zero at entry.
//
//   2. `rcu_read_unlock()` — must be balanced by a prior
//      `rcu_read_lock()`.  CBMC checks that depth > 0 at entry.
//
//   3. `rcu_read_lock()` — call site marker (no contract on this
//      direction; only depth+1 ensures).
// @@

@ synchronize_rcu_call @
position p;
@@

synchronize_rcu@p()

@ script:python synchronize_rcu_report @
p << synchronize_rcu_call.p;
@@

coccilib.report.print_report(p[0],
    "rcu_critical_section: synchronize_rcu call site — candidate "
    "for CBMC property scan (must run outside RCU read-side "
    "critical section; CVE class: sleeping-in-RCU-CS)")

@ rcu_read_unlock_call @
position p;
@@

rcu_read_unlock@p()

@ script:python rcu_read_unlock_report @
p << rcu_read_unlock_call.p;
@@

coccilib.report.print_report(p[0],
    "rcu_critical_section: rcu_read_unlock call site — candidate "
    "for CBMC property scan (must be balanced; CVE class: "
    "unbalanced rcu_read_unlock)")

@ rcu_read_lock_call @
position p;
@@

rcu_read_lock@p()

@ script:python rcu_read_lock_report @
p << rcu_read_lock_call.p;
@@

coccilib.report.print_report(p[0],
    "rcu_critical_section: rcu_read_lock call site — candidate "
    "for CBMC property scan (paired-balance check)")
