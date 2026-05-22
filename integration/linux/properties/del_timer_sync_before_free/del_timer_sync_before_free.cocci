// @@
//   SmPL rule: del_timer_sync_before_free.cocci
// @@

@ timer_setup_call @
expression t, fn, flags;
position p;
@@

timer_setup@p(t, fn, flags);

@ script:python timer_setup_report @
p << timer_setup_call.p;
@@

coccilib.report.print_report(p[0],
    "del_timer_sync_before_free: timer_setup call site — "
    "candidate for CBMC property scan (cancel-before-free bug "
    "class on timer_list)")

@ kfree_after_timer_setup @
expression x, fn, flags;
position p;
@@

timer_setup(&x->t, fn, flags);
... when != del_timer_sync(&x->t)
    when != del_timer(&x->t)
    when != timer_delete_sync(&x->t)
    when != timer_delete(&x->t)
kfree@p(x);

@ script:python kfree_after_timer_setup_report @
p << kfree_after_timer_setup.p;
@@

coccilib.report.print_report(p[0],
    "del_timer_sync_before_free: BUG-SHAPE: timer_setup(&x->t, "
    "...) ... kfree(x) without intervening del_timer_sync / "
    "timer_delete_sync (cancel-before-free UAF bug class on "
    "timer_list)")
