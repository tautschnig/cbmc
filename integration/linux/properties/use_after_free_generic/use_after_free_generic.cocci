// @@
//   SmPL rule: use_after_free_generic.cocci
//
//   Two complementary rules:
//   1. kfree-then-deref:  kfree(p); ... p->X
//   2. kfree-then-kfree:  kfree(p); ... kfree(p)
// @@

@ kfree_then_deref @
expression x, f;
position p;
@@

kfree(x);
... when != x = ...
    when != if (x ...) { ... }
x->f@p

@ script:python kfree_then_deref_report @
p << kfree_then_deref.p;
@@

coccilib.report.print_report(p[0],
    "use_after_free_generic: kfree(x) followed by x->field "
    "without intervening reassignment (CVE class: "
    "use_after_free)")

@ kfree_then_kfree @
expression x;
position p;
@@

kfree(x);
... when != x = ...
kfree@p(x);

@ script:python kfree_then_kfree_report @
p << kfree_then_kfree.p;
@@

coccilib.report.print_report(p[0],
    "use_after_free_generic: kfree(x); kfree(x) — explicit "
    "double-free (CVE class: double_free_or_unlock)")

@ kvfree_then_kvfree @
expression x;
position p;
@@

kvfree(x);
... when != x = ...
kvfree@p(x);

@ script:python kvfree_then_kvfree_report @
p << kvfree_then_kvfree.p;
@@

coccilib.report.print_report(p[0],
    "use_after_free_generic: kvfree(x); kvfree(x) — explicit "
    "double-free (CVE class: double_free_or_unlock)")
