// @@
//   SmPL rule: lock_state.cocci
//
//   Coccinelle prefilter for the lock_state property module.
//   Flags every `mutex_unlock` / `spin_unlock` call site — any
//   such site is a candidate for lock-balance review.
//
//   The prefilter is deliberately coarse.  CBMC's per-file
//   harness (scan/scan-per-file.sh lock_state FILE FUNC) is the
//   precision layer: for a given function, it runs the
//   synthesised harness against the lock_state contract and
//   reports per-call-site verdicts.
//
//   Separate rules for common kernel lock APIs; extend here as
//   new APIs appear.
// @@

@ mutex_unlock_call @
expression lock;
position p;
@@

mutex_unlock@p(lock);

@ script:python mutex_unlock_report @
p << mutex_unlock_call.p;
@@

coccilib.report.print_report(p[0],
    "lock_state: mutex_unlock call site — candidate for CBMC "
    "property scan (double-mutex-unlock / unlock-without-lock "
    "bug class)")

@ spin_unlock_call @
expression lock;
position p;
@@

spin_unlock@p(lock);

@ script:python spin_unlock_report @
p << spin_unlock_call.p;
@@

coccilib.report.print_report(p[0],
    "lock_state: spin_unlock call site — candidate for CBMC "
    "property scan (double-spin-unlock / unlock-without-lock "
    "bug class)")
