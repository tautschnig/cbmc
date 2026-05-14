// @@
//   SmPL rule: refcount_lifetime.cocci
//
//   Coccinelle prefilter for the refcount_lifetime property
//   module.  Two complementary levels:
//
//   1. CALL-SITE rule: flags every call to
//      `refcount_dec_and_test`.  High recall, low precision.
//      Tagged "candidate" in the report message.
//
//   2. BUG-SHAPE rule: flags only the actual underflow shape —
//      `refcount_dec_and_test(r); ... refcount_dec_and_test(r)`
//      with no intervening `refcount_inc(r)` or
//      `refcount_inc_not_zero(r)`.  Tagged "BUG-SHAPE:".
// @@

// ---------- level 1: call sites (high recall) ----------

@ refcount_dec_and_test_call @
expression ref;
position p;
@@

refcount_dec_and_test@p(ref)

@ script:python refcount_dec_and_test_report @
p << refcount_dec_and_test_call.p;
@@

coccilib.report.print_report(p[0],
    "refcount_lifetime: refcount_dec_and_test call site — "
    "candidate for CBMC property scan (refcount underflow / "
    "double-dec bug class)")

// ---------- level 2: bug shapes (high precision) ----------

@ double_dec @
expression r;
position p;
@@

refcount_dec_and_test(r);
... when != refcount_inc(r)
    when != refcount_inc_not_zero(r)
    when != refcount_set(r, ...)
refcount_dec_and_test@p(r);

@ script:python double_dec_report @
p << double_dec.p;
@@

coccilib.report.print_report(p[0],
    "refcount_lifetime: BUG-SHAPE: double refcount_dec_and_test(r) "
    "without intervening refcount_inc / refcount_set "
    "(refcount underflow class)")
