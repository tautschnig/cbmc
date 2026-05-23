// Coccinelle instrumentation rule for use_after_free_generic.
//
// Inserts __assert_not_freed(x) before x->fld = E
// assignment-statements reached after kfree(x) (or kvfree,
// kfree_skb) without intervening reassignment of x.

@@
expression x, e;
identifier fld;
@@

\( kfree(x); \| kvfree(x); \| kfree_skb(x); \)
... when != x = ...
+ __assert_not_freed(x);
x->fld = e;
