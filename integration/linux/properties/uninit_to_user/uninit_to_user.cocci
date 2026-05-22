// @@
//   SmPL rule: uninit_to_user.cocci
//
//   Cocci finds copy_to_user / put_user / nla_put call sites
//   where the source is a stack-allocated struct without an
//   intervening memset.  This is a heuristic — not every
//   struct without a memset is a leak (some explicitly
//   initialise every field) — so candidate sites need triage.
// @@

@ struct_then_copy_to_user @
type T;
identifier x;
expression user_ptr;
position p;
@@

T x;
... when != memset(&x, 0, ...)
    when != x = (T) { ... }
copy_to_user@p(user_ptr, &x, sizeof(x))

@ script:python struct_then_copy_to_user_report @
p << struct_then_copy_to_user.p;
@@

coccilib.report.print_report(p[0],
    "uninit_to_user: copy_to_user from a stack struct without "
    "an intervening memset.  Verify every field is initialised "
    "before the copy (CVE class: uninit_or_info_leak).")

@ struct_then_nla_put @
type T;
identifier x;
expression skb, attrtype;
position p;
@@

T x;
... when != memset(&x, 0, ...)
    when != x = (T) { ... }
nla_put@p(skb, attrtype, sizeof(x), &x)

@ script:python struct_then_nla_put_report @
p << struct_then_nla_put.p;
@@

coccilib.report.print_report(p[0],
    "uninit_to_user: nla_put from a stack struct without "
    "an intervening memset.  Verify every field is initialised "
    "(CVE class: uninit_or_info_leak).")
