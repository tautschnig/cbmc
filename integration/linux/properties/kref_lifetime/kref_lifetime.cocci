// @@
//   SmPL rule: kref_lifetime.cocci
//
//   Coccinelle prefilter for the kref_lifetime property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every call to `kref_put`.
//      High recall, low precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule: flag back-to-back `kref_put(x) ...
//      kref_put(x)` with no intervening `kref_get(x)`.
//      Tagged "BUG-SHAPE:".
//
//   Motivating CVE class: generic kref UAFs (30+ CVE descriptions mention kref_put; kref is the foundational primitive that many type-specific lifetime modules wrap).
// @@

@ kref_put_call @
expression x;
position p;
@@

kref_put@p(x);

@ script:python kref_put_report @
p << kref_put_call.p;
@@

coccilib.report.print_report(p[0],
    "kref_lifetime: kref_put call site — candidate for "
    "CBMC property scan (use-after-put bug class on "
    "struct kref)")

@ back_to_back_kref_put @
expression x;
position p;
@@

kref_put(x);
... when != kref_get(x)
    when != \(x = \( kref_get(...) \| ... \)\)
kref_put@p(x);

@ script:python back_to_back_kref_put_report @
p << back_to_back_kref_put.p;
@@

coccilib.report.print_report(p[0],
    "kref_lifetime: BUG-SHAPE: back-to-back kref_put(x) "
    "... kref_put(x) without intervening "
    "kref_get(x) (double-put / unbalanced put)")
