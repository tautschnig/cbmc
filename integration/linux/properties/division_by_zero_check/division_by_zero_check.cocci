// @@
//   SmPL rule: division_by_zero_check.cocci
// @@

@ div_by_var @
expression a, b;
position p;
@@

a / b@p

@ script:python div_by_var_report @
p << div_by_var.p;
@@

coccilib.report.print_report(p[0],
    "division_by_zero_check: division by a variable — "
    "candidate for triage (verify divisor is non-zero on "
    "every reachable path).  CVE class: dos_panic_warn.")

@ mod_by_var @
expression a, b;
position p;
@@

a % b@p

@ script:python mod_by_var_report @
p << mod_by_var.p;
@@

coccilib.report.print_report(p[0],
    "division_by_zero_check: modulo by a variable — same "
    "concern as division by zero.")

@ div_helpers @
expression x, d;
position p;
@@

\(do_div@p(x, d)
\| div_u64@p(x, d)
\| div_s64@p(x, d)
\| div64_u64@p(x, d)
\)

@ script:python div_helpers_report @
p << div_helpers.p;
@@

coccilib.report.print_report(p[0],
    "division_by_zero_check: kernel-helper division — verify "
    "divisor is non-zero.  CVE class: dos_panic_warn.")
