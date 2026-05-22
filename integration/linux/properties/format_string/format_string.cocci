// @@
//   SmPL rule: format_string.cocci
// @@

@ printk_with_var_fmt @
expression x;
position p;
@@

\(printk@p(x)
\| pr_info@p(x)
\| pr_err@p(x)
\| pr_warn@p(x)
\| dev_info@p(x)
\| seq_printf@p(x)
\)

@ script:python printk_with_var_fmt_report @
p << printk_with_var_fmt.p;
@@

coccilib.report.print_report(p[0],
    "format_string: printk-family with a single argument — "
    "verify the argument is a constant format string, not "
    "attacker-controlled (CVE class: format_string).")
