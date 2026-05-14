// @@
//   SmPL rule: cred_lifetime.cocci
//
//   Coccinelle prefilter for the cred_lifetime property module.
//
//   Two complementary levels of recall vs precision:
//
//   1. CALL-SITE rules (`put_cred_call`, `under_put_cred_call`):
//      flag every call to `put_cred` / `__put_cred`.  High
//      recall, low precision — useful as a generic candidate
//      list when we don't yet know what bug shape we're looking
//      for.  Tagged "candidate" in the report message.
//
//   2. BUG-SHAPE rules (`back_to_back_put`, `back_to_back_under_put`):
//      flag only the actual bug-class shape — back-to-back
//      `put_cred(c) ... put_cred(c)` with no intervening
//      `get_cred(c)`.  These are real candidates, not just
//      call sites.  Tagged "BUG-SHAPE:" in the report message
//      so consumers (e.g. bug-hunt with `--bug-shape-only`)
//      can filter.
//
//   The CVE-2026-23297 class lives in level 2.
// @@

// ---------- level 1: call sites (high recall) ----------

@ put_cred_call @
expression cred;
position p;
@@

put_cred@p(cred);

@ script:python put_cred_report @
p << put_cred_call.p;
@@

coccilib.report.print_report(p[0],
    "cred_lifetime: put_cred call site — candidate for CBMC "
    "property scan (use-after-put-cred bug class, "
    "CVE-2026-23297 class)")

@ under_put_cred_call @
expression cred;
position p;
@@

__put_cred@p(cred);

@ script:python under_put_cred_report @
p << under_put_cred_call.p;
@@

coccilib.report.print_report(p[0],
    "cred_lifetime: __put_cred call site — candidate for CBMC "
    "property scan (use-after-put-cred bug class, "
    "CVE-2026-23297 class)")

// ---------- level 2: bug shapes (high precision) ----------

@ back_to_back_put @
expression c;
position p;
@@

put_cred(c);
... when != get_cred(c)
    when != \(c = \( get_cred(...) \| ... \)\)
put_cred@p(c);

@ script:python back_to_back_put_report @
p << back_to_back_put.p;
@@

coccilib.report.print_report(p[0],
    "cred_lifetime: BUG-SHAPE: back-to-back put_cred(c) ... "
    "put_cred(c) without intervening get_cred(c) "
    "(CVE-2026-23297-class double-put)")

@ back_to_back_under_put @
expression c;
position p;
@@

__put_cred(c);
... when != get_cred(c)
    when != \(c = \( get_cred(...) \| ... \)\)
__put_cred@p(c);

@ script:python back_to_back_under_put_report @
p << back_to_back_under_put.p;
@@

coccilib.report.print_report(p[0],
    "cred_lifetime: BUG-SHAPE: back-to-back __put_cred(c) ... "
    "__put_cred(c) without intervening get_cred(c) "
    "(CVE-2026-23297-class double-put)")
