/// \file
/// uninit_to_user.h — property module for "uninitialised kernel
/// data copied to userspace".
///
/// ## Bug class
///
/// A kernel buffer is partially initialised, then `copy_to_user`
/// (or `put_user`, `nla_put`, `skb_put_data`) sends it to
/// userspace.  The uninitialised bytes leak kernel memory —
/// stack contents, heap residue, pointer values — to an
/// attacker.
///
/// ```c
/// struct kernel_info info;
/// info.field_a = ...;
/// // forgot info.field_b
/// copy_to_user(user_ptr, &info, sizeof(info));   // BUG
/// ```
///
/// Fix: `memset(&info, 0, sizeof(info));` before populating, or
/// initialise every field explicitly.
///
/// Motivating CVE class: `uninit_or_info_leak` from the
/// 2023-2026 kernel CVE survey (44 CVEs, 0.5% — small but
/// high-severity).  Concrete recent example: CVE-2026-22978
/// (wifi: avoid kernel-infoleak from struct iw_point).
///
/// ## Abstraction
///
/// Per-pointer ghost flag `fully_initialised`.  Set by
/// `mark_initialised(p)` (the harness calls this after a
/// `memset(p, 0, sz)` or after explicitly initialising every
/// field); checked by the contract on
/// `__assert_safe_for_userspace(p)`.

#ifndef INTEGRATION_LINUX_PROPERTIES_UNINIT_TO_USER_H
#define INTEGRATION_LINUX_PROPERTIES_UNINIT_TO_USER_H

void mark_initialised(const void *p);
void mark_uninitialised(const void *p);
int is_initialised(const void *p);

#endif
