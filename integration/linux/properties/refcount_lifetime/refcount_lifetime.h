/// \file
/// refcount_lifetime.h — property module for Linux kernel
/// `refcount_t` underflow / double-dec bugs.
///
/// ## Bug class
///
/// Many kernel objects use the `refcount_t` API (introduced as
/// a safer replacement for raw `atomic_t` refcounts) to track
/// ownership.  Canonical pattern:
///
///     struct foo { refcount_t refs; ... };
///     refcount_inc(&foo->refs);                 // bump
///     if (refcount_dec_and_test(&foo->refs))    // drop; free if 0
///         free_foo(foo);
///
/// Two recurring error modes show up across CVEs:
///
///   1. **Underflow double-dec.**  A path calls
///      `refcount_dec_and_test(r)` on an already-zero counter.
///      The refcount_t API saturates rather than wraps, but
///      the object was already freed on the first dec — the
///      second dec is the UAF.  See numerous io_uring,
///      crypto/keysetup, and sk_buff regressions.
///
///   2. **Leaked reference.**  An error path returns without
///      `refcount_dec_and_test`ing a counter it previously
///      `refcount_inc`remented.  Leaks kernel memory.
///
/// This module exposes the ghost state keyed on the
/// `refcount_t *` address and a `refcount_live(r)` predicate
/// that can be used as a precondition on any API that
/// decrements.  Conceptually a direct template of
/// cred_lifetime, but keyed on a `refcount_t *` rather than
/// on a `struct cred *` so it generalises across every kernel
/// object using the refcount_t API.
///
/// ## Abstraction
///
/// `refcount_t` is left as an opaque forward-declaration.
/// The property only uses the pointer identity.  At link time
/// with a real kernel TU, the kernel's full
/// `<linux/refcount.h>` / `<linux/refcount_types.h>` typedef
/// is unified in.

#ifndef INTEGRATION_LINUX_PROPERTIES_REFCOUNT_LIFETIME_REFCOUNT_LIFETIME_H
#define INTEGRATION_LINUX_PROPERTIES_REFCOUNT_LIFETIME_REFCOUNT_LIFETIME_H

#include <stddef.h>

// Opaque forward-declaration (see LIM-016 for the rationale
// behind keeping property-module public structs opaque to
// avoid link-time structural conflicts with kernel headers).
typedef struct refcount_struct refcount_t;

// Ghost state API.  Stores per-refcount_t usage count keyed by
// pointer identity, independent of the real `refcount_t`'s
// internal atomic field.
void refcount_lifetime_init(refcount_t *r, unsigned int usage);
void refcount_lifetime_inc(refcount_t *r);
// Decrement; returns 1 if the counter reached zero, else 0.
int refcount_lifetime_dec_and_test(refcount_t *r);
unsigned int refcount_lifetime_usage(refcount_t *r);

// Predicate: the refcount is live (usage > 0).  Safe to call
// inside a `__CPROVER_requires` clause.
int refcount_live(refcount_t *r);

#endif
