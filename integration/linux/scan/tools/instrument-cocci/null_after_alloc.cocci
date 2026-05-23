// Coccinelle instrumentation rule for null_after_alloc.
//
// Inserts __assert_safe_to_deref(x) before each x->field = E
// assignment that follows a kmalloc-family allocation.
//
// Restricted to assignment-statement context (rather than
// arbitrary expression context) so the inserted statement is
// always at a valid statement boundary — avoiding the
// inside-an-expression cocci insertion pitfall.
//
// CBMC's path exploration handles the safe-vs-vuln
// distinction: paths where the allocation returns non-NULL
// satisfy the contract; NULL-return paths fire it.

@ assign @
expression x, e1, e2, e3, e4;
identifier fld;
@@

x =
\( kmalloc(e1, e2)
\| kzalloc(e1, e2)
\| kcalloc(e1, e2, e3)
\| alloc_skb(e1, e2)
\| kmem_cache_alloc(e1, e2)
\| vmalloc(e1)
\| kvmalloc(e1, e2)
\);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;

@ decl @
type T;
identifier x, fld;
expression e1, e2, e3, e4;
@@

T x =
\( kmalloc(e1, e2)
\| kzalloc(e1, e2)
\| kcalloc(e1, e2, e3)
\| alloc_skb(e1, e2)
\| kmem_cache_alloc(e1, e2)
\| vmalloc(e1)
\| kvmalloc(e1, e2)
\);
... when != x = e1
+ __assert_safe_to_deref(x);
x->fld = e4;
