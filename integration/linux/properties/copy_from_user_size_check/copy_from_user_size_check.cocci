// @@
//   SmPL rule: copy_from_user_size_check.cocci
//
//   Coccinelle prefilter for copy_from_user calls.  All call
//   sites are flagged as candidates for triage; the bug class
//   requires hand-checking that `len` is bounded against the
//   destination buffer capacity.
// @@

@ copy_from_user_call @
expression dst, src, len;
position p;
@@

copy_from_user@p(dst, src, len)

@ script:python copy_from_user_report @
p << copy_from_user_call.p;
@@

coccilib.report.print_report(p[0],
    "copy_from_user_size_check: copy_from_user call site — "
    "candidate for CBMC property scan (CVE class: "
    "string_or_copy_bound).  Verify the third argument is "
    "bounded against the destination's capacity.")

@ get_user_call @
expression v, p;
position pos;
@@

get_user@pos(v, p)

@ script:python get_user_report @
pos << get_user_call.pos;
@@

coccilib.report.print_report(pos[0],
    "copy_from_user_size_check: get_user call site — type-"
    "size determines copy width; flag for triage if v is a "
    "wide type accessed via a narrow user pointer.")
