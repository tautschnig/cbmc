// @@
//   SmPL rule: inode_lifetime.cocci
//
//   Coccinelle prefilter for the inode_lifetime property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every call to `iput`.
//      High recall, low precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule: flag back-to-back `iput(x) ...
//      iput(x)` with no intervening `ihold(x)`.
//      Tagged "BUG-SHAPE:".
//
//   Motivating CVE class: filesystem inode UAFs (50+ CVE descriptions mention iput).
// @@

@ iput_call @
expression x;
position p;
@@

iput@p(x);

@ script:python iput_report @
p << iput_call.p;
@@

coccilib.report.print_report(p[0],
    "inode_lifetime: iput call site — candidate for "
    "CBMC property scan (use-after-put bug class on "
    "struct inode)")

@ back_to_back_iput @
expression x;
position p;
@@

iput(x);
... when != ihold(x)
    when != \(x = \( ihold(...) \| ... \)\)
iput@p(x);

@ script:python back_to_back_iput_report @
p << back_to_back_iput.p;
@@

coccilib.report.print_report(p[0],
    "inode_lifetime: BUG-SHAPE: back-to-back iput(x) "
    "... iput(x) without intervening "
    "ihold(x) (double-put / unbalanced put)")
