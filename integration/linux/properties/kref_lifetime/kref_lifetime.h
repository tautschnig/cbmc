/// \file
/// kref_lifetime.h — property module for Linux kernel
/// `struct kref` refcount / use-after-put bugs.
///
/// ## Bug class
///
/// The kernel manages `struct kref` lifetimes through a
/// reference count: `kref_get(kref)`
/// increments it; `kref_put(kref)`
/// decrements and, when the count reaches zero, releases the
/// object.  Two related bug shapes recur:
///
///   1. **Double put.**  An error path puts an object that an
///      earlier success path already put, dropping the refcount
///      below the live threshold and freeing memory some other
///      code path still holds.
///
///   2. **Put without a matching get.**  Code obtains a reference
///      by other means (sysfs, container_of on a list pointer)
///      and then puts it.  Same UAF outcome.
///
/// Motivating CVE family: generic kref UAFs (30+ CVE descriptions mention kref_put; kref is the foundational primitive that many type-specific lifetime modules wrap).
/// Subsystem focus: `kernel/, drivers/*, fs/*`.
///
/// ## Abstraction
///
/// `struct kref` is left as a forward declaration in this
/// module's public header; the full kernel definition is unified
/// in at link time.  The property only ever uses pointer identity
/// against the ghost table — it never dereferences fields.

#ifndef INTEGRATION_LINUX_PROPERTIES_KREF_LIFETIME_KREF_LIFETIME_H
#define INTEGRATION_LINUX_PROPERTIES_KREF_LIFETIME_KREF_LIFETIME_H

#include <stddef.h>

// `struct kref` is left opaque (see kobject_lifetime.h's
// note for rationale: structurally embedding a partial definition
// here would conflict with the kernel's full struct at link time
// and re-introduce LIM-016).  TUs that need a concrete instance
// supply their own definition; scan adapters get the kernel's
// full struct via the kernel TU at link time.
struct kref;

// Ghost state API.
void kref_lifetime_init(struct kref *kref,
                              unsigned int usage);
void kref_lifetime_get(struct kref *kref);
void kref_lifetime_put(struct kref *kref);
unsigned int kref_lifetime_usage(struct kref *kref);

// Predicate: the object is live (refcount > 0, has not been
// freed).  Safe to call inside a `__CPROVER_requires` clause.
int kref_live(struct kref *kref);

#endif
