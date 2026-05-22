/// \file
/// null_after_alloc.h — property module for "dereference of a
/// possibly-NULL allocator return without first checking".
///
/// ## Bug class
///
/// Allocators like `kmalloc` / `kzalloc` / `alloc_skb` /
/// `kobject_create` / `nla_nest_start` etc. return NULL on
/// failure (out of memory).  The standard kernel idiom is to
/// check the return before dereferencing:
///
/// ```c
/// struct foo *p = kmalloc(sizeof(*p), GFP_KERNEL);
/// if (!p)
///     return -ENOMEM;
/// p->field = 42;     // safe
/// ```
///
/// Bugs occur when the caller skips the check:
///
/// ```c
/// struct foo *p = kmalloc(sizeof(*p), GFP_KERNEL);
/// p->field = 42;     // BUG: NULL deref if alloc failed
/// ```
///
/// Motivating CVE class: `null_pointer_deref` from the
/// 2023-2026 kernel CVE survey — **1,035 CVEs (11.9% of
/// classified volume), the largest single bug-shape
/// category**.  Concrete recent examples: CVE-2026-43471
/// (scsi/ufs), CVE-2026-43441 (net/bonding), CVE-2026-43431
/// (xhci debugfs), CVE-2026-43422 (usb gadget).
///
/// ## Abstraction
///
/// Per-pointer ghost flag `null_checked`.  Set by an
/// `assert_null_check_done(p)` call after a successful
/// non-NULL test; queried by the contract on
/// `assert_safe_to_deref(p)` (the synthetic checkpoint the
/// harness places before each dereference of an allocator
/// return).
///
/// ## What this module covers
///
/// * **Direct-call harness** with vuln/fix shapes.
/// * **Cocci prefilter** for `p = kmalloc(...) ... p->X`
///   patterns without an intervening NULL check.
///
/// ## What this module does NOT cover (yet)
///
/// * **Per-file synthesis** on real kernel functions.  The
///   per-file harness can't auto-instrument every `p->X`
///   site to call the checkpoint; that needs either a
///   goto-instrument pass or per-site cocci-driven
///   instrumentation.  Cocci is the primary integration
///   path.
/// * **Allocators we don't enumerate.**  The cocci file
///   names the common kernel allocators; rare allocators
///   are missed.

#ifndef INTEGRATION_LINUX_PROPERTIES_NULL_AFTER_ALLOC_H
#define INTEGRATION_LINUX_PROPERTIES_NULL_AFTER_ALLOC_H

void assert_null_check_done(const void *p);
void assert_null_check_clear(const void *p);
int null_check_done(const void *p);

#endif
