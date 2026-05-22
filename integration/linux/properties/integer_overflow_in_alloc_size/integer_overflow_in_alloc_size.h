/// \file
/// integer_overflow_in_alloc_size.h — property module for
/// "n * sizeof passed to kmalloc without overflow check".
///
/// ## Bug class
///
/// `kmalloc(n * sizeof(elem), GFP_KERNEL)` where `n` is
/// attacker-controlled and `n * sizeof(elem)` overflows
/// `size_t`.  The result is a small allocation followed by
/// out-of-bounds writes.  The fix is `kmalloc_array(n,
/// sizeof(elem), GFP_KERNEL)` which uses
/// `check_mul_overflow` internally.
///
/// ```c
/// // BUG:
/// arr = kmalloc(n * sizeof(*arr), GFP_KERNEL);
///
/// // Fix:
/// arr = kmalloc_array(n, sizeof(*arr), GFP_KERNEL);
/// // OR explicit guard:
/// if (n > SIZE_MAX / sizeof(*arr))
///     return -ENOMEM;
/// arr = kmalloc(n * sizeof(*arr), GFP_KERNEL);
/// ```
///
/// Motivating CVE class: `integer_overflow` from the
/// 2023-2026 kernel CVE survey — **122 CVEs (1.4% of
/// classified volume).**  Concrete recent examples:
/// CVE-2026-43301 (media wave5 PM runtime usage count
/// underflow), CVE-2026-43286 (mm/hugetlb).
///
/// ## Abstraction
///
/// The contract `__assert_size_safe_for_alloc(n, elem_size)`
/// requires that `n * elem_size` does not overflow.

#ifndef INTEGRATION_LINUX_PROPERTIES_INTEGER_OVERFLOW_IN_ALLOC_SIZE_H
#define INTEGRATION_LINUX_PROPERTIES_INTEGER_OVERFLOW_IN_ALLOC_SIZE_H

#include <stddef.h>

int alloc_size_safe(size_t n, size_t elem_size);

#endif
