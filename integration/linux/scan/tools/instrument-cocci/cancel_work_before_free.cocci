// Coccinelle rule for cancel_work_before_free per-file
// instrumentation.
//
// Tracks INIT_WORK calls and inserts ghost-state markers:
//   * cancel_work_set_pending(W) after INIT_WORK(W, ...).
//   * cancel_work_clear_pending(W) after cancel_work_sync(W).
//   * __assert_no_pending_work(W) before kfree of the
//     containing object — emitted only when an INIT_WORK
//     was seen on a work_struct embedded in the freed object.
//
// The matcher is conservative: it only fires when the
// INIT_WORK and kfree are syntactically adjacent in the
// same function with no intervening cancel_work_sync.

@ init_work_track @
expression W;
expression FN, DATA;
@@
INIT_WORK(W, FN);
+ cancel_work_set_pending(W);

@ cancel_track @
expression W;
@@
cancel_work_sync(W);
+ cancel_work_clear_pending(W);

@ delayed_init @
expression W;
expression FN;
@@
INIT_DELAYED_WORK(W, FN);
+ cancel_work_set_pending(&(W)->work);

@ delayed_cancel @
expression W;
@@
cancel_delayed_work_sync(W);
+ cancel_work_clear_pending(&(W)->work);

// ---- assert before kfree of containing object ----
//
// Pattern: INIT_WORK(&obj->fld, ...) ... kfree(obj);
// without an intervening cancel_work_sync.  The cocci CFG
// pass will only fire this on simple cases; the per-return
// fallback handles complex ones.

@ assert_before_kfree @
expression OBJ;
identifier FLD;
expression FN;
@@
INIT_WORK(&(OBJ)->FLD, FN);
... when != cancel_work_sync(&(OBJ)->FLD)
    when != cancel_work_clear_pending(&(OBJ)->FLD)
+ __assert_no_pending_work(&(OBJ)->FLD);
kfree(OBJ);
