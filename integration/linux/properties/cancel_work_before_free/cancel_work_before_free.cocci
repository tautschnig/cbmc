// @@
//   SmPL rule: cancel_work_before_free.cocci
//
//   Coccinelle prefilter for "free of a struct containing a
//   work_struct without first cancelling pending work".
//
//   Two rules:
//
//   1. CALL-SITE rule: flag every `INIT_WORK` call site as a
//      candidate.  High recall, low precision.
//
//   2. BUG-SHAPE rule: flag the actual bug class — `INIT_WORK
//      (&x->work, ...) ... kfree(x)` with no intervening
//      `cancel_work_sync(&x->work)`.
//
//   Motivating CVE class: cancel-before-free UAFs (51 CVEs
//   classified `cleanup_ordering` in the 2023-2026 survey;
//   many more in `use_after_free`).
// @@

@ init_work_call @
expression work, fn;
position p;
@@

INIT_WORK@p(work, fn);

@ script:python init_work_report @
p << init_work_call.p;
@@

coccilib.report.print_report(p[0],
    "cancel_work_before_free: INIT_WORK call site — candidate "
    "for CBMC property scan (cancel-before-free bug class)")

@ kfree_after_init_work @
expression x, work, fn;
position p;
@@

INIT_WORK(&x->work, fn);
... when != cancel_work_sync(&x->work)
    when != cancel_work_sync(work)
kfree@p(x);

@ script:python kfree_after_init_work_report @
p << kfree_after_init_work.p;
@@

coccilib.report.print_report(p[0],
    "cancel_work_before_free: BUG-SHAPE: INIT_WORK(&x->work, ...) "
    "... kfree(x) without intervening cancel_work_sync(&x->work) "
    "(cancel-before-free UAF bug class)")
