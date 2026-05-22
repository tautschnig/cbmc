/// \file
/// permission_bypass.h — property module for "privileged
/// operation without capability check".
///
/// ## Bug class
///
/// A handler invokes a privileged operation but doesn't first
/// check the caller's capability:
///
/// ```c
/// int handler(struct request *req) {
///     // BUG: missing capable(CAP_NET_ADMIN) check
///     do_privileged_thing(req);
///     return 0;
/// }
/// ```
///
/// Fix: gate the operation with `capable(CAP_X)`,
/// `ns_capable(ns, CAP_X)`, or similar.
///
/// Motivating CVE class: `permission_bypass` from the v3
/// CVE survey (15 CVEs, 0.2 % of classified volume).  Small
/// but high-severity (privilege escalation).  Concrete
/// recent: CVE-2026-23268 (apparmor unprivileged-policy-
/// management), CVE-2025-21702 (pfifo_tail_enqueue).
///
/// ## Abstraction
///
/// Single boolean ghost `__cap_checked` set by helpers that
/// model a successful capability check.  Adapter contracts
/// `__assert_privileged(...)` require the bit is set.

#ifndef INTEGRATION_LINUX_PROPERTIES_PERMISSION_BYPASS_H
#define INTEGRATION_LINUX_PROPERTIES_PERMISSION_BYPASS_H

extern int __cap_checked;

void cap_check_passed(void);
void cap_check_clear(void);
int cap_was_checked(void);

#endif
