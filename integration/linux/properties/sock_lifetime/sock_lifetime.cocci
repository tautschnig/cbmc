// @@
//   SmPL rule: sock_lifetime.cocci
//
//   Coccinelle prefilter for the sock_lifetime property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every call to `sock_put`.
//      High recall, low precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule: flag back-to-back `sock_put(x) ...
//      sock_put(x)` with no intervening `sock_hold(x)`.
//      Tagged "BUG-SHAPE:".
//
//   Motivating CVE class: AF_VSOCK / netlink / Bluetooth socket UAFs (24+ CVE descriptions mention sock_put, 18+ mention sock_hold).
// @@

@ sock_put_call @
expression x;
position p;
@@

sock_put@p(x);

@ script:python sock_put_report @
p << sock_put_call.p;
@@

coccilib.report.print_report(p[0],
    "sock_lifetime: sock_put call site — candidate for "
    "CBMC property scan (use-after-put bug class on "
    "struct sock)")

@ back_to_back_sock_put @
expression x;
position p;
@@

sock_put(x);
... when != sock_hold(x)
    when != \(x = \( sock_hold(...) \| ... \)\)
sock_put@p(x);

@ script:python back_to_back_sock_put_report @
p << back_to_back_sock_put.p;
@@

coccilib.report.print_report(p[0],
    "sock_lifetime: BUG-SHAPE: back-to-back sock_put(x) "
    "... sock_put(x) without intervening "
    "sock_hold(x) (double-put / unbalanced put)")
