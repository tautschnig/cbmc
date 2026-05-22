// @@
//   SmPL rule: resource_leak_on_error_path.cocci
//
//   Coccinelle prefilter for "alloc + early-return-without-free"
//   patterns.  Two complementary rules cover kmalloc and
//   alloc_skb.
// @@

@ kmalloc_then_early_return @
expression x, sz, flags;
position p;
@@

x = kmalloc(sz, flags);
... when != kfree(x)
    when != kvfree(x)
return@p ...;

@ script:python kmalloc_then_early_return_report @
p << kmalloc_then_early_return.p;
@@

coccilib.report.print_report(p[0],
    "resource_leak_on_error_path: kmalloc result returned "
    "without intervening kfree (CVE class: resource_leak)")

@ kzalloc_then_early_return @
expression x, sz, flags;
position p;
@@

x = kzalloc(sz, flags);
... when != kfree(x)
    when != kvfree(x)
return@p ...;

@ script:python kzalloc_then_early_return_report @
p << kzalloc_then_early_return.p;
@@

coccilib.report.print_report(p[0],
    "resource_leak_on_error_path: kzalloc result returned "
    "without intervening kfree (CVE class: resource_leak)")

@ alloc_skb_then_early_return @
expression x, sz, flags;
position p;
@@

x = alloc_skb(sz, flags);
... when != kfree_skb(x)
    when != consume_skb(x)
return@p ...;

@ script:python alloc_skb_then_early_return_report @
p << alloc_skb_then_early_return.p;
@@

coccilib.report.print_report(p[0],
    "resource_leak_on_error_path: alloc_skb result returned "
    "without intervening kfree_skb / consume_skb (CVE class: "
    "resource_leak)")
