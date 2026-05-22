// @@
//   SmPL rule: null_after_alloc.cocci
//
//   Coccinelle prefilter for "allocate then dereference without
//   a NULL check".  Covers the most common kernel allocators.
// @@

@ kmalloc_then_deref @
expression x, sz, flags, f;
position p;
@@

x = kmalloc(sz, flags);
... when != if (!x ...) { ... }
    when != if (x == NULL ...) { ... }
    when != if (IS_ERR(x)) { ... }
x->f@p

@ script:python kmalloc_then_deref_report @
p << kmalloc_then_deref.p;
@@

coccilib.report.print_report(p[0],
    "null_after_alloc: dereference of kmalloc result without "
    "intervening NULL check (CVE class: null_pointer_deref)")

@ kzalloc_then_deref @
expression x, sz, flags, f;
position p;
@@

x = kzalloc(sz, flags);
... when != if (!x ...) { ... }
    when != if (x == NULL ...) { ... }
x->f@p

@ script:python kzalloc_then_deref_report @
p << kzalloc_then_deref.p;
@@

coccilib.report.print_report(p[0],
    "null_after_alloc: dereference of kzalloc result without "
    "intervening NULL check (CVE class: null_pointer_deref)")

@ kcalloc_then_deref @
expression x, n, sz, flags, f;
position p;
@@

x = kcalloc(n, sz, flags);
... when != if (!x ...) { ... }
    when != if (x == NULL ...) { ... }
x->f@p

@ script:python kcalloc_then_deref_report @
p << kcalloc_then_deref.p;
@@

coccilib.report.print_report(p[0],
    "null_after_alloc: dereference of kcalloc result without "
    "intervening NULL check (CVE class: null_pointer_deref)")

@ alloc_skb_then_deref @
expression x, sz, flags, f;
position p;
@@

x = alloc_skb(sz, flags);
... when != if (!x ...) { ... }
    when != if (x == NULL ...) { ... }
x->f@p

@ script:python alloc_skb_then_deref_report @
p << alloc_skb_then_deref.p;
@@

coccilib.report.print_report(p[0],
    "null_after_alloc: dereference of alloc_skb result without "
    "intervening NULL check (CVE class: null_pointer_deref)")
