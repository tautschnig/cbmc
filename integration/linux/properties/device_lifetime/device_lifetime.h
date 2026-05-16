/// \file
/// device_lifetime.h — property module for Linux kernel
/// `struct device` refcount / use-after-put bugs.
///
/// ## Bug class
///
/// The kernel manages `struct device` lifetimes through a
/// reference count: `get_device(dev)`
/// increments it; `put_device(dev)`
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
/// Motivating CVE family: driver-core UAFs (CVE family: 82+ CVE descriptions mention put_device, the most-cited refcount API in the 2023-2026 kernel CVE record).
/// Subsystem focus: `drivers/*`.
///
/// ## Abstraction
///
/// `struct device` is left as a forward declaration in this
/// module's public header; the full kernel definition is unified
/// in at link time.  The property only ever uses pointer identity
/// against the ghost table — it never dereferences fields.

#ifndef INTEGRATION_LINUX_PROPERTIES_DEVICE_LIFETIME_DEVICE_LIFETIME_H
#define INTEGRATION_LINUX_PROPERTIES_DEVICE_LIFETIME_DEVICE_LIFETIME_H

#include <stddef.h>

// `struct device` is left opaque (see kobject_lifetime.h's
// note for rationale: structurally embedding a partial definition
// here would conflict with the kernel's full struct at link time
// and re-introduce LIM-016).  TUs that need a concrete instance
// supply their own definition; scan adapters get the kernel's
// full struct via the kernel TU at link time.
struct device;

// Ghost state API.
void device_lifetime_init(struct device *dev,
                              unsigned int usage);
void device_lifetime_get(struct device *dev);
void device_lifetime_put(struct device *dev);
unsigned int device_lifetime_usage(struct device *dev);

// Predicate: the object is live (refcount > 0, has not been
// freed).  Safe to call inside a `__CPROVER_requires` clause.
int device_live(struct device *dev);

#endif
