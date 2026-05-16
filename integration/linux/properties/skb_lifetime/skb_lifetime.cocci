// @@
//   SmPL rule: skb_lifetime.cocci
//
//   Coccinelle prefilter for the skb_lifetime property module.
//   Two complementary levels:
//
//   1. CALL-SITE rules: flag every call to `kfree_skb`.
//      High recall, low precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule: flag back-to-back `kfree_skb(x) ...
//      kfree_skb(x)` with no intervening `skb_get(x)`.
//      Tagged "BUG-SHAPE:".
//
//   Motivating CVE class: sk_buff UAFs in network stacks and drivers/net (16+ mention skb_put, 12+ mention skb_get).
// @@

@ kfree_skb_call @
expression x;
position p;
@@

kfree_skb@p(x);

@ script:python kfree_skb_report @
p << kfree_skb_call.p;
@@

coccilib.report.print_report(p[0],
    "skb_lifetime: kfree_skb call site — candidate for "
    "CBMC property scan (use-after-put bug class on "
    "struct sk_buff)")

@ back_to_back_kfree_skb @
expression x;
position p;
@@

kfree_skb(x);
... when != skb_get(x)
    when != \(x = \( skb_get(...) \| ... \)\)
kfree_skb@p(x);

@ script:python back_to_back_kfree_skb_report @
p << back_to_back_kfree_skb.p;
@@

coccilib.report.print_report(p[0],
    "skb_lifetime: BUG-SHAPE: back-to-back kfree_skb(x) "
    "... kfree_skb(x) without intervening "
    "skb_get(x) (double-put / unbalanced put)")
