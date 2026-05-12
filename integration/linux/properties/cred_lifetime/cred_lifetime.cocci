// @@
//   SmPL rule: cred_lifetime.cocci
//
//   Coccinelle prefilter for the cred_lifetime property module.
//   Flags every call site of `put_cred` / `__put_cred` — any
//   such site is a candidate for cred-lifetime review, because
//   the CVE-2026-23297-class bug surface lives at "was the cred
//   still live at this put?" and "is the cred still live after
//   this put?".
//
//   Precision is CBMC's job: when spatch reports a hit, running
//   scan.py's cred_lifetime kernel adapter pins the
//   `cred_live(c)` precondition on `put_cred` and the
//   direct-call harness exercises the use-after-put-cred shape.
//
//   Two rule variants covering the two common kernel APIs.
// @@

@ put_cred_call @
expression cred;
position p;
@@

put_cred@p(cred);

@ script:python put_cred_report @
p << put_cred_call.p;
@@

coccilib.report.print_report(p[0],
    "cred_lifetime: put_cred call site — candidate for CBMC "
    "property scan (use-after-put-cred bug class, "
    "CVE-2026-23297 class)")

@ under_put_cred_call @
expression cred;
position p;
@@

__put_cred@p(cred);

@ script:python under_put_cred_report @
p << under_put_cred_call.p;
@@

coccilib.report.print_report(p[0],
    "cred_lifetime: __put_cred call site — candidate for CBMC "
    "property scan (use-after-put-cred bug class, "
    "CVE-2026-23297 class)")
