// @@
//   SmPL rule: dentry_lifetime.cocci
//
//   Coccinelle prefilter for the dentry_lifetime property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every call to `dput`.
//      High recall, low precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule: flag back-to-back `dput(x) ...
//      dput(x)` with no intervening `dget(x)`.
//      Tagged "BUG-SHAPE:".
//
//   Motivating CVE class: filesystem dentry UAFs (47+ CVE descriptions mention dput).
// @@

@ dput_call @
expression x;
position p;
@@

dput@p(x);

@ script:python dput_report @
p << dput_call.p;
@@

coccilib.report.print_report(p[0],
    "dentry_lifetime: dput call site — candidate for "
    "CBMC property scan (use-after-put bug class on "
    "struct dentry)")

@ back_to_back_dput @
expression x;
position p;
@@

dput(x);
... when != dget(x)
    when != \(x = \( dget(...) \| ... \)\)
dput@p(x);

@ script:python back_to_back_dput_report @
p << back_to_back_dput.p;
@@

coccilib.report.print_report(p[0],
    "dentry_lifetime: BUG-SHAPE: back-to-back dput(x) "
    "... dput(x) without intervening "
    "dget(x) (double-put / unbalanced put)")
