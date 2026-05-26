// Coccinelle instrumentation rule for resource_leak_on_error_path.
//
// Tracks an allocation x via leak_alloc_track(x); then
// asserts __assert_no_leak_at_exit(x) before each return
// statement reached without an intervening kfree(x) or
// reassignment.
//
// Each alloc-API has its own rule pair (assign + decl) to
// keep cocci's per-rule CFG analysis bounded — combining
// them in a single \( ... \| ... \) alternation can cause
// "inconsistent control-flow paths" errors on functions
// with many return statements, which makes cocci abandon
// the whole file rather than just skip the troublesome
// rule.

// ---- kmalloc ----
@ kmalloc_assign @
expression x;
expression e1, e2;
@@
x = kmalloc(e1, e2);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != kfree_skb(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kmalloc_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = kmalloc(e1, e2);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != kfree_skb(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

// ---- kzalloc ----
@ kzalloc_assign @
expression x;
expression e1, e2;
@@
x = kzalloc(e1, e2);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != kfree_skb(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kzalloc_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = kzalloc(e1, e2);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != kfree_skb(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

// ---- kcalloc ----
@ kcalloc_assign @
expression x;
expression e1, e2, e3;
@@
x = kcalloc(e1, e2, e3);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ kcalloc_decl @
type T;
identifier x;
expression e1, e2, e3;
@@
T x = kcalloc(e1, e2, e3);
+ leak_alloc_track(x);
... when != kfree(x)
    when != kvfree(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

// ---- alloc_skb ----
@ alloc_skb_assign @
expression x;
expression e1, e2;
@@
x = alloc_skb(e1, e2);
+ leak_alloc_track(x);
... when != kfree_skb(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;

@ alloc_skb_decl @
type T;
identifier x;
expression e1, e2;
@@
T x = alloc_skb(e1, e2);
+ leak_alloc_track(x);
... when != kfree_skb(x)
    when != x = e1
+ __assert_no_leak_at_exit(x);
return ...;
