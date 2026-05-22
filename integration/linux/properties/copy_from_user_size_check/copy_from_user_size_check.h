/// \file
/// copy_from_user_size_check.h — property module for
/// "copy_from_user with unbounded len".
///
/// ## Bug class
///
/// `copy_from_user(dst, src, len)` where `len` is
/// user-controlled and not bounded against the destination's
/// capacity, leading to a stack/heap buffer overflow:
///
/// ```c
/// char buf[64];
/// if (copy_from_user(buf, user_ptr, user_len))   // BUG
///     return -EFAULT;
/// ```
///
/// Fix: validate `user_len <= sizeof(buf)` first, or use
/// `min_t(size_t, user_len, sizeof(buf))`.
///
/// Motivating CVE class: `string_or_copy_bound` from the
/// 2023-2026 kernel CVE survey — **27 CVEs (0.3% of
/// classified volume).**  Smaller bucket but very high-
/// severity (stack OOB writes from userspace).
///
/// ## Abstraction
///
/// Predicate `copy_len_safe(dst_capacity, len)` returns 1
/// iff `len <= dst_capacity`.  The adapter contracts a
/// `__assert_copy_safe(cap, len)` checkpoint that the
/// harness places before the copy.

#ifndef INTEGRATION_LINUX_PROPERTIES_COPY_FROM_USER_SIZE_CHECK_H
#define INTEGRATION_LINUX_PROPERTIES_COPY_FROM_USER_SIZE_CHECK_H

#include <stddef.h>

int copy_len_safe(size_t dst_capacity, size_t len);

#endif
