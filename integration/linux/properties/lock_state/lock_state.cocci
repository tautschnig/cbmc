// @@
//   SmPL rule: lock_state.cocci
//
//   Coccinelle prefilter for the lock_state property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every mutex_unlock / spin_unlock
//      / read_unlock / write_unlock call site.  High recall,
//      low precision — useful as a generic candidate list.
//      Tagged "candidate" in the report message.
//
//   2. BUG-SHAPE rules: flag the actual unbalanced-unlock
//      shapes —
//        - double-unlock: `mutex_unlock(m); ... mutex_unlock(m)`
//          with no intervening `mutex_lock(m)`
//        - same for spin_unlock / spin_lock
//      Tagged "BUG-SHAPE:" so consumers can filter.
//
//   "unlock-without-prior-lock" is harder to express in cocci
//   (would need negative function-entry context); it falls out
//   of the per-file scan when the harness's lock_state ghost
//   has the lock unheld at the call site.
// @@

// ---------- level 1: call sites ----------

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

// ---------- level 2: bug shapes ----------

@ double_mutex_unlock @
expression m;
position p;
@@

mutex_unlock(m);
... when != mutex_lock(m)
    when != mutex_lock_nested(m, ...)
    when != mutex_lock_interruptible(m)
    when != mutex_trylock(m)
mutex_unlock@p(m);

@ script:python double_mutex_unlock_report @
p << double_mutex_unlock.p;
@@

coccilib.report.print_report(p[0],
    "lock_state: BUG-SHAPE: double mutex_unlock(m) ... "
    "mutex_unlock(m) without intervening mutex_lock(m) "
    "(double-unlock class)")

@ double_spin_unlock @
expression s;
position p;
@@

spin_unlock(s);
... when != spin_lock(s)
    when != spin_lock_irq(s)
    when != spin_lock_irqsave(s, ...)
    when != spin_lock_bh(s)
    when != spin_trylock(s)
spin_unlock@p(s);

@ script:python double_spin_unlock_report @
p << double_spin_unlock.p;
@@

coccilib.report.print_report(p[0],
    "lock_state: BUG-SHAPE: double spin_unlock(s) ... "
    "spin_unlock(s) without intervening spin_lock(s) "
    "(double-unlock class)")
