// @@
//   SmPL rule: integer_overflow_in_alloc_size.cocci
//
//   Coccinelle prefilter for kmalloc(n * sizeof(...)) where
//   n is variable.  Recommends kmalloc_array.
// @@

@ kmalloc_n_times_sizeof @
expression n, T, flags;
position p;
@@

kmalloc@p(n * sizeof(T), flags)

@ script:python kmalloc_n_times_sizeof_report @
p << kmalloc_n_times_sizeof.p;
@@

coccilib.report.print_report(p[0],
    "integer_overflow_in_alloc_size: kmalloc(n * sizeof(...)) "
    "without overflow check (CVE class: integer_overflow). "
    "Use kmalloc_array(n, sizeof(...), GFP_*) which has a "
    "built-in check_mul_overflow.")

@ kzalloc_n_times_sizeof @
expression n, T, flags;
position p;
@@

kzalloc@p(n * sizeof(T), flags)

@ script:python kzalloc_n_times_sizeof_report @
p << kzalloc_n_times_sizeof.p;
@@

coccilib.report.print_report(p[0],
    "integer_overflow_in_alloc_size: kzalloc(n * sizeof(...)) "
    "without overflow check.  Use kcalloc(n, sizeof(...), "
    "GFP_*) which has a built-in check_mul_overflow.")
