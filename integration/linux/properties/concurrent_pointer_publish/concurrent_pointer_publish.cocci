// @@
//   SmPL rule: concurrent_pointer_publish.cocci
//
//   Coccinelle prefilter for "publish-before-init" race bug
//   shapes.  Two complementary rules:
//
//   1. Plain assignment to a "ready" / "valid" / "active"
//      / "live" flag followed by data writes on the same
//      object — suspicious order, candidate for race.
//
//   2. WRITE_ONCE / smp_store_release pairs where the
//      release write happens before its data writes.  This
//      is harder to flag mechanically; the rule below is a
//      heuristic.
//
//   Motivating CVE class: 232 CVEs in the 2023-2026 kernel
//   CVE survey fall under race_or_toctoue.
// @@

@ ready_before_data @
expression x;
expression v;
identifier f =~ "^(ready|valid|active|live|published|initialised|enabled)$";
identifier g;
position p;
@@

x->f@p = 1;
... when != smp_store_release(&x->f, ...)
    when != cpp_publish()
x->g = v;

@ script:python ready_before_data_report @
p << ready_before_data.p;
@@

coccilib.report.print_report(p[0],
    "concurrent_pointer_publish: a 'ready/valid/active' field "
    "is set BEFORE its sibling data fields are written. "
    "Concurrent readers may see ready=1 before the data is "
    "valid (race_or_toctoue bug class).  Use "
    "smp_store_release / WRITE_ONCE-after-init or initialise "
    "the data fields first.")
