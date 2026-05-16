// @@
//   SmPL rule: kobject_lifetime.cocci
//
//   Coccinelle prefilter for the kobject_lifetime property
//   module.  Two complementary levels:
//
//   1. CALL-SITE rules (`kobject_put_call`): flag every call to
//      `kobject_put` / `kobject_del`.  High recall, low
//      precision.  Tagged "candidate".
//
//   2. BUG-SHAPE rule (`back_to_back_kobject_put`): flag the
//      actual bug-class shape — `kobject_put(k) ... kobject_put(k)`
//      with no intervening `kobject_get(k)`.  Tagged "BUG-SHAPE:".
//
//   Common kobject UAF / refcount patterns (matching examples in
//   the kernel CVE record):
//   - Error path puts a kobject already put on the success path.
//   - sysfs handler puts the kobject the framework will also put.
//   - Container kobject put before children are released.
// @@

// ---------- level 1: call sites (high recall) ----------

@ kobject_put_call @
expression k;
position p;
@@

kobject_put@p(k);

@ script:python kobject_put_report @
p << kobject_put_call.p;
@@

coccilib.report.print_report(p[0],
    "kobject_lifetime: kobject_put call site — candidate for "
    "CBMC property scan (use-after-put-kobject bug class, "
    "kobject double-put / unbalanced put)")

@ kobject_del_call @
expression k;
position p;
@@

kobject_del@p(k);

@ script:python kobject_del_report @
p << kobject_del_call.p;
@@

coccilib.report.print_report(p[0],
    "kobject_lifetime: kobject_del call site — candidate for "
    "CBMC property scan (kobject_del often pairs with kobject_put; "
    "ordering bugs lead to UAF)")

// ---------- level 2: bug shapes (high precision) ----------

@ back_to_back_kobject_put @
expression k;
position p;
@@

kobject_put(k);
... when != kobject_get(k)
    when != \(k = \( kobject_get(...) \| ... \)\)
kobject_put@p(k);

@ script:python back_to_back_kobject_put_report @
p << back_to_back_kobject_put.p;
@@

coccilib.report.print_report(p[0],
    "kobject_lifetime: BUG-SHAPE: back-to-back kobject_put(k) ... "
    "kobject_put(k) without intervening kobject_get(k) "
    "(double-put / unbalanced put)")
