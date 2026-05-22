/// \file
/// division_by_zero_check.h — property module for division-by-
/// zero kernel panics.
///
/// ## Bug class
///
/// `x / divisor` or `x % divisor` where `divisor` could be
/// zero — divides by zero raise a `#DE` fault on x86,
/// crashing the kernel.  Common shape:
///
/// ```c
/// rate = total_jiffies / hz_value;   // BUG if hz_value == 0
/// ```
///
/// Fix: validate `divisor != 0` before the division, or use
/// `DIV_ROUND_UP_ULL` etc. with a guard.
///
/// Motivating CVE class: subset of `dos_panic_warn` (249
/// CVEs, 2.9% of classified volume in the 2023-2026 kernel
/// CVE survey).  Concrete recent example: CVE-2026-43354
/// (iio proximity hx9023s sample-frequency divide-by-zero).
///
/// ## Abstraction
///
/// Predicate `divisor_safe(d)` returns 1 iff `d != 0`.

#ifndef INTEGRATION_LINUX_PROPERTIES_DIVISION_BY_ZERO_CHECK_H
#define INTEGRATION_LINUX_PROPERTIES_DIVISION_BY_ZERO_CHECK_H

int divisor_safe(long long d);

#endif
