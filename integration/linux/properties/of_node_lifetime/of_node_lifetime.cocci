// @@
//   SmPL rule: of_node_lifetime.cocci
//
//   Coccinelle prefilter for the of_node_lifetime property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every call to `of_node_put`.
//      High recall, low precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule: flag back-to-back `of_node_put(x) ...
//      of_node_put(x)` with no intervening `of_node_get(x)`.
//      Tagged "BUG-SHAPE:".
//
//   Motivating CVE class: Open Firmware / device tree refcount UAFs (55+ CVE descriptions mention of_node_put).
// @@

@ of_node_put_call @
expression x;
position p;
@@

of_node_put@p(x);

@ script:python of_node_put_report @
p << of_node_put_call.p;
@@

coccilib.report.print_report(p[0],
    "of_node_lifetime: of_node_put call site — candidate for "
    "CBMC property scan (use-after-put bug class on "
    "struct device_node)")

@ back_to_back_of_node_put @
expression x;
position p;
@@

of_node_put(x);
... when != of_node_get(x)
    when != \(x = \( of_node_get(...) \| ... \)\)
of_node_put@p(x);

@ script:python back_to_back_of_node_put_report @
p << back_to_back_of_node_put.p;
@@

coccilib.report.print_report(p[0],
    "of_node_lifetime: BUG-SHAPE: back-to-back of_node_put(x) "
    "... of_node_put(x) without intervening "
    "of_node_get(x) (double-put / unbalanced put)")
