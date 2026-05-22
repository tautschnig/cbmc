// @@
//   SmPL rule: concurrent_double_put.cocci
//
//   Coccinelle prefilter for concurrent-double-put race bug
//   shapes.  Flags "if-live-then-put" patterns where the
//   liveness check and the put are not atomic relative to
//   other threads.
// @@

@ check_then_put @
expression p;
identifier check =~ "^(.*_(live|active|present|ref|alive))$";
identifier put =~ "^(.*_put|put_.*)$";
position pos;
@@

if (check(p)) {
    ...
    put@pos(p);
    ...
}

@ script:python check_then_put_report @
pos << check_then_put.pos;
@@

coccilib.report.print_report(pos[0],
    "concurrent_double_put: 'if-live-then-put' pattern — "
    "check and put are not atomic.  Concurrent threads can "
    "both pass the check before either's put runs, leading to "
    "a double-put race.  Use refcount_dec_and_test or atomic "
    "RCU-protected access (race_or_toctoue bug class).")
