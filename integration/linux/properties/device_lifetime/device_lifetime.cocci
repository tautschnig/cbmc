// @@
//   SmPL rule: device_lifetime.cocci
//
//   Coccinelle prefilter for the device_lifetime property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every call to `put_device`.
//      High recall, low precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule: flag back-to-back `put_device(x) ...
//      put_device(x)` with no intervening `get_device(x)`.
//      Tagged "BUG-SHAPE:".
//
//   Motivating CVE class: driver-core UAFs (CVE family: 82+ CVE descriptions mention put_device, the most-cited refcount API in the 2023-2026 kernel CVE record).
// @@

@ put_device_call @
expression x;
position p;
@@

put_device@p(x);

@ script:python put_device_report @
p << put_device_call.p;
@@

coccilib.report.print_report(p[0],
    "device_lifetime: put_device call site — candidate for "
    "CBMC property scan (use-after-put bug class on "
    "struct device)")

@ back_to_back_put_device @
expression x;
position p;
@@

put_device(x);
... when != get_device(x)
    when != \(x = \( get_device(...) \| ... \)\)
put_device@p(x);

@ script:python back_to_back_put_device_report @
p << back_to_back_put_device.p;
@@

coccilib.report.print_report(p[0],
    "device_lifetime: BUG-SHAPE: back-to-back put_device(x) "
    "... put_device(x) without intervening "
    "get_device(x) (double-put / unbalanced put)")
