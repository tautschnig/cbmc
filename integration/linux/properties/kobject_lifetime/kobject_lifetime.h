/// \file
/// kobject_lifetime.h — property module for Linux kernel
/// `struct kobject` refcount / use-after-put bugs.
///
/// ## Bug class
///
/// The kernel manages `struct kobject` lifetimes through a `kref`
/// embedded in the object: `kobject_get(k)` increments the
/// refcount and returns the same pointer; `kobject_put(k)`
/// decrements and, when the count reaches zero, calls the type's
/// release function and frees `k`.  Two related bug shapes show
/// up in the CVE record:
///
///   1. **Double put.**  An error path puts a kobject that an
///      earlier success path already put, dropping the refcount
///      below zero (typically a UAF-class bug because
///      kobject_put then frees the object that some other code
///      path still holds a reference to).
///
///   2. **Put without a matching get.**  Code that obtains a
///      kobject reference by other means (e.g. container_of on a
///      sysfs pointer the framework hasn't kobject_get'd for the
///      caller) and then puts it.  Same UAF outcome.
///
/// The shape mirrors `cred_lifetime` exactly: a ghost table keyed
/// by pointer identity tracks each kobject's refcount, with
/// `kobject_get` / `kobject_put` adjusting the ghost in lockstep
/// with the kernel's real refcount semantics, and a predicate
/// `kobject_live(k)` (ghost usage > 0) that scan adapters use as
/// the contract precondition on the kernel's `kobject_put`.
///
/// ## Abstraction
///
/// `struct kobject` is left as a forward declaration in this
/// module's public header; the full kernel definition is unified
/// in at link time.  The property only ever uses pointer identity
/// against the ghost table — it never dereferences kobject
/// fields.

#ifndef INTEGRATION_LINUX_PROPERTIES_KOBJECT_LIFETIME_KOBJECT_LIFETIME_H
#define INTEGRATION_LINUX_PROPERTIES_KOBJECT_LIFETIME_KOBJECT_LIFETIME_H

#include <stddef.h>

// `struct kobject` is left opaque (see cred_lifetime.h's note for
// rationale: structurally embedding a partial definition here
// would conflict with the kernel's full struct at link time and
// re-introduce LIM-016).  TUs that need a concrete instance
// (abstract unit tests, abstract CVE harnesses) supply their own
// definition; scan adapters get the kernel's full struct via the
// kernel TU at link time.
struct kobject;

// Ghost state API.
void kobject_lifetime_init(struct kobject *k, unsigned int usage);
void kobject_lifetime_get(struct kobject *k);
void kobject_lifetime_put(struct kobject *k);
unsigned int kobject_lifetime_usage(struct kobject *k);

// Predicate: the kobject is live (refcount > 0, has not been
// freed).  Safe to call inside a `__CPROVER_requires` clause.
int kobject_live(struct kobject *k);

#endif
