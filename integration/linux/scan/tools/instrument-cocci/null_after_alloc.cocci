// Coccinelle instrumentation rule for null_after_alloc.
//
// Inserts __assert_safe_to_deref(x) before each x->field = E
// assignment that follows a kmalloc-family allocation.
//
// Restricted to assignment-statement context (rather than
// arbitrary expression context) so the inserted statement is
// always at a valid statement boundary.
//
// CBMC's path exploration handles the safe-vs-vuln
// distinction: paths where the allocation returns non-NULL
// satisfy the contract; NULL-return paths fire it.
//
// Per-API rule pairs (assign + decl) for each allocator
// to keep cocci's CFG analysis bounded.

// ---- kmalloc / kzalloc / kcalloc / kmalloc_array ----

@ kmalloc_assign @
expression x, e1, e2, e4;
identifier fld;
@@
x = kmalloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kmalloc_decl @
type T;
identifier x, fld;
expression e1, e2, e4;
@@
T x = kmalloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kzalloc_assign @
expression x, e1, e2, e4;
identifier fld;
@@
x = kzalloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kzalloc_decl @
type T;
identifier x, fld;
expression e1, e2, e4;
@@
T x = kzalloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kcalloc_assign @
expression x, e1, e2, e3, e4;
identifier fld;
@@
x = kcalloc(e1, e2, e3);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kcalloc_decl @
type T;
identifier x, fld;
expression e1, e2, e3, e4;
@@
T x = kcalloc(e1, e2, e3);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kmalloc_array_assign @
expression x, e1, e2, e3, e4;
identifier fld;
@@
x = kmalloc_array(e1, e2, e3);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kmalloc_array_decl @
type T;
identifier x, fld;
expression e1, e2, e3, e4;
@@
T x = kmalloc_array(e1, e2, e3);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

// ---- kvmalloc / kvzalloc / kvmalloc_array / kvcalloc ----

@ kvmalloc_assign @
expression x, e1, e2, e4;
identifier fld;
@@
x = kvmalloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kvmalloc_decl @
type T;
identifier x, fld;
expression e1, e2, e4;
@@
T x = kvmalloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kvzalloc_assign @
expression x, e1, e2, e4;
identifier fld;
@@
x = kvzalloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kvzalloc_decl @
type T;
identifier x, fld;
expression e1, e2, e4;
@@
T x = kvzalloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kvcalloc_assign @
expression x, e1, e2, e3, e4;
identifier fld;
@@
x = kvcalloc(e1, e2, e3);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kvcalloc_decl @
type T;
identifier x, fld;
expression e1, e2, e3, e4;
@@
T x = kvcalloc(e1, e2, e3);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

// ---- vmalloc / vzalloc ----

@ vmalloc_assign @
expression x, e1, e4;
identifier fld;
@@
x = vmalloc(e1);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ vmalloc_decl @
type T;
identifier x, fld;
expression e1, e4;
@@
T x = vmalloc(e1);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ vzalloc_assign @
expression x, e1, e4;
identifier fld;
@@
x = vzalloc(e1);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ vzalloc_decl @
type T;
identifier x, fld;
expression e1, e4;
@@
T x = vzalloc(e1);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

// ---- alloc_skb ----

@ alloc_skb_assign @
expression x, e1, e2, e4;
identifier fld;
@@
x = alloc_skb(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ alloc_skb_decl @
type T;
identifier x, fld;
expression e1, e2, e4;
@@
T x = alloc_skb(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

// ---- kmem_cache_alloc / kmem_cache_zalloc ----

@ kmem_cache_alloc_assign @
expression x, e1, e2, e4;
identifier fld;
@@
x = kmem_cache_alloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kmem_cache_alloc_decl @
type T;
identifier x, fld;
expression e1, e2, e4;
@@
T x = kmem_cache_alloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kmem_cache_zalloc_assign @
expression x, e1, e2, e4;
identifier fld;
@@
x = kmem_cache_zalloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kmem_cache_zalloc_decl @
type T;
identifier x, fld;
expression e1, e2, e4;
@@
T x = kmem_cache_zalloc(e1, e2);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

// ---- kmemdup / kstrdup / kasprintf ----

@ kmemdup_assign @
expression x, e1, e2, e3, e4;
identifier fld;
@@
x = kmemdup(e1, e2, e3);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ kmemdup_decl @
type T;
identifier x, fld;
expression e1, e2, e3, e4;
@@
T x = kmemdup(e1, e2, e3);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;
