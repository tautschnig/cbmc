/// \file
/// cred_lifetime.h — property module for Linux kernel `struct cred`
/// refcount / use-after-put bugs.
///
/// ## Bug class
///
/// The kernel manages `struct cred` via the atomic refcount in
/// `cred->usage`.  `get_cred(c)` increments it; `put_cred(c)`
/// decrements and, if the result reaches zero, frees `c`.  Two
/// related error patterns show up repeatedly in the CVE record:
///
///   1. **Use after put.**  Code path `put_cred(c); … *c = …;`
///      dereferences `c` after the refcount may have dropped to
///      zero and the cred been freed.  If the caller did not hold
///      an additional reference, this is a classic UAF.
///
///   2. **Leaked reference.**  An error path returns without
///      `put_cred`ing a cred it previously got.  The CVE-2026-23297
///      `nfsd_nl_threads_set_doit` leak is the motivating concrete
///      example: `get_cred()` succeeds, a subsequent step fails,
///      and the error-exit skips `put_cred`.
///
/// This module models the shared ghost state (per-cred usage
/// count) needed for both patterns, and exposes a predicate
/// `cred_live(c)` (usage > 0) that can be used as the precondition
/// on any cred-consuming API.  The direct-call harness under
/// `scan/adapters/cred_kernel_direct_harness.c` exercises the
/// use-after-put pattern against the kernel's `put_cred`.
///
/// ## Abstraction
///
/// `struct cred` is modelled with just the `usage` field present;
/// the rest of the kernel's struct is opaque padding.  That's
/// enough for both the unit test and the scan adapter, since the
/// property only ever reads `usage`.

#ifndef INTEGRATION_LINUX_PROPERTIES_CRED_LIFETIME_CRED_LIFETIME_H
#define INTEGRATION_LINUX_PROPERTIES_CRED_LIFETIME_CRED_LIFETIME_H

#include <stddef.h>

// Minimal kernel-layout-compatible struct cred.  The kernel's real
// struct has many more fields; we only ever read `usage`, and the
// adapter re-declares the same `usage`-in-first-field layout so
// linking with a real kernel TU unifies cleanly.  `struct cred` is
// a complete type (one-word padding) so non-kernel callers — the
// unit test and the abstract CVE harnesses — can stack-allocate
// sentinels for ghost-table identity.
struct cred
{
  // Matches the kernel's `atomic_t usage;` as a plain unsigned
  // int for abstract reasoning; the property does not care about
  // atomicity.
  unsigned int usage;
  unsigned long _pad;
};

// Ghost state API.  The property module stores a per-cred usage
// count keyed by pointer identity, independent of what the
// struct's `usage` field actually holds — the ghost is what the
// property checks against, avoiding false positives from any
// nondet writes cbmc's symbolic execution might pick for the
// real field.
void cred_lifetime_init(struct cred *c, unsigned int usage);
void cred_lifetime_get(struct cred *c);
void cred_lifetime_put(struct cred *c);
unsigned int cred_lifetime_usage(struct cred *c);

// Predicate: the cred is live (refcount > 0, i.e. has not been
// freed).  Safe to call inside a `__CPROVER_requires` clause.
int cred_live(struct cred *c);

#endif
