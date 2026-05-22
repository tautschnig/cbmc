/// \file
/// format_string.h — property module for "user-supplied
/// format string passed to printk-family functions".
///
/// ## Bug class
///
/// `printk`, `seq_printf`, `snprintf`, etc. take a format
/// string as the first argument.  If that string is
/// attacker-controlled, format-specifier injection enables
/// arbitrary kernel reads (`%s`, `%n`).
///
/// ```c
/// // BUG: format string is attacker-controlled
/// printk(user_supplied_string);
///
/// // Fix: use a constant format string with %s.
/// printk("%s", user_supplied_string);
/// ```
///
/// Motivating CVE class: `format_string` from the v3 CVE
/// survey (3 CVEs, 0.0 % — small but high-severity).
/// Concrete recent: CVE-2025-68816 (mlx5 fw_tracer
/// validate format-string parameters).
///
/// ## Abstraction
///
/// Predicate `format_is_constant(p)` — returns 1 iff `p`
/// is known at compile time (modelled via a per-pointer
/// flag the harness sets explicitly).

#ifndef INTEGRATION_LINUX_PROPERTIES_FORMAT_STRING_H
#define INTEGRATION_LINUX_PROPERTIES_FORMAT_STRING_H

void mark_format_constant(const char *fmt);
void mark_format_tainted(const char *fmt);
int format_is_constant(const char *fmt);

#endif
