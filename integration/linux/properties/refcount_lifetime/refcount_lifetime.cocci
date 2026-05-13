// @@
//   SmPL rule: refcount_lifetime.cocci
//
//   Coccinelle prefilter for the refcount_lifetime property
//   module.  Flags every call site of
//   `refcount_dec_and_test(r)` — any such site is a candidate
//   for refcount-lifetime review, because the underflow-class
//   bug surface lives at "was the refcount still live at this
//   dec?".
//
//   Precision is CBMC's job: when spatch reports a hit, running
//   scan.py's refcount_lifetime kernel adapter pins the
//   `refcount_live(r)` precondition on
//   `refcount_dec_and_test` and the direct-call harness
//   exercises the double-dec shape.
//
//   One rule variant matches the canonical
//   `refcount_dec_and_test` API.  `refcount_sub_and_test`
//   and `refcount_inc_not_zero` have analogous bug classes
//   and may be added later as additional rules.
// @@

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
