// @@
//   SmPL rule: cancel_delayed_work_before_free.cocci
//
//   Coccinelle prefilter for "free of a struct containing a
//   delayed_work without first cancelling pending delayed work".
//   Mirrors cancel_work_before_free.cocci but for
//   INIT_DELAYED_WORK / cancel_delayed_work_sync.
// @@

@ init_delayed_work_call @
expression dwork, fn;
position p;
@@

INIT_DELAYED_WORK@p(dwork, fn);

@ script:python init_delayed_work_report @
p << init_delayed_work_call.p;
@@

coccilib.report.print_report(p[0],
    "cancel_delayed_work_before_free: INIT_DELAYED_WORK call site "
    "— candidate for CBMC property scan (cancel-before-free bug "
    "class on delayed_work)")

@ kfree_after_init_dwork @
expression x, fn;
position p;
@@

INIT_DELAYED_WORK(&x->dwork, fn);
... when != cancel_delayed_work_sync(&x->dwork)
    when != cancel_delayed_work(&x->dwork)
kfree@p(x);

@ script:python kfree_after_init_dwork_report @
p << kfree_after_init_dwork.p;
@@

coccilib.report.print_report(p[0],
    "cancel_delayed_work_before_free: BUG-SHAPE: "
    "INIT_DELAYED_WORK(&x->dwork, ...) ... kfree(x) without "
    "intervening cancel_delayed_work_sync(&x->dwork) (cancel-"
    "before-free UAF bug class on delayed_work)")
