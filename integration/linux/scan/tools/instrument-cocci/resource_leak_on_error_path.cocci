// Coccinelle instrumentation rule for resource_leak_on_error_path.
//
// Tracks an allocation x via leak_alloc_track(x); then
// asserts __assert_no_leak_at_exit(x) before each return
// statement reached without an intervening kfree(x) or
// reassignment.
//
// Only inserts at top-level statement positions (after
// alloc statement, before return statement) — avoiding
// inside-an-expression insertions.

@ assign @
expression x;
expression e1, e2, e3;
@@

x =
\( kmalloc(e1, e2)
\| kzalloc(e1, e2)
\| kcalloc(e1, e2, e3)
\| alloc_skb(e1, e2)
\| kmem_cache_alloc(e1, e2)
\);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != kfree_skb(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ decl @
type T;
identifier x;
expression e1, e2, e3;
@@

T x =
\( kmalloc(e1, e2)
\| kzalloc(e1, e2)
\| kcalloc(e1, e2, e3)
\| alloc_skb(e1, e2)
\| kmem_cache_alloc(e1, e2)
\);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != kfree_skb(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;
