// @@
//   SmPL rule: fput_lifetime.cocci
//
//   Coccinelle prefilter for the fput_lifetime property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every call to `fput`.
//      High recall, low precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule: flag back-to-back `fput(x) ...
//      fput(x)` with no intervening `get_file(x)`.
//      Tagged "BUG-SHAPE:".
//
//   Motivating CVE class: file struct UAFs / fdput races (34+ CVE descriptions mention fput).
// @@

@ fput_call @
expression x;
position p;
@@

fput@p(x);

@ script:python fput_report @
p << fput_call.p;
@@

coccilib.report.print_report(p[0],
    "fput_lifetime: fput call site — candidate for "
    "CBMC property scan (use-after-put bug class on "
    "struct file)")

@ back_to_back_fput @
expression x;
position p;
@@

fput(x);
... when != get_file(x)
    when != \(x = \( get_file(...) \| ... \)\)
fput@p(x);

@ script:python back_to_back_fput_report @
p << back_to_back_fput.p;
@@

coccilib.report.print_report(p[0],
    "fput_lifetime: BUG-SHAPE: back-to-back fput(x) "
    "... fput(x) without intervening "
    "get_file(x) (double-put / unbalanced put)")
