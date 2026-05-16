// @@
//   SmPL rule: module_lifetime.cocci
//
//   Coccinelle prefilter for the module_lifetime property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every call to `module_put`.
//      High recall, low precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule: flag back-to-back `module_put(x) ...
//      module_put(x)` with no intervening `try_module_get(x)`.
//      Tagged "BUG-SHAPE:".
//
//   Motivating CVE class: module reference leaks blocking module unload (11+ CVE descriptions mention try_module_get).
// @@

@ module_put_call @
expression x;
position p;
@@

module_put@p(x);

@ script:python module_put_report @
p << module_put_call.p;
@@

coccilib.report.print_report(p[0],
    "module_lifetime: module_put call site — candidate for "
    "CBMC property scan (use-after-put bug class on "
    "struct module)")

@ back_to_back_module_put @
expression x;
position p;
@@

module_put(x);
... when != try_module_get(x)
    when != \(x = \( try_module_get(...) \| ... \)\)
module_put@p(x);

@ script:python back_to_back_module_put_report @
p << back_to_back_module_put.p;
@@

coccilib.report.print_report(p[0],
    "module_lifetime: BUG-SHAPE: back-to-back module_put(x) "
    "... module_put(x) without intervening "
    "try_module_get(x) (double-put / unbalanced put)")
