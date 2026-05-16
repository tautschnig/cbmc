/// \file
/// of_node_lifetime.h — property module for Linux kernel
/// `struct device_node` refcount / use-after-put bugs.
///
/// ## Bug class
///
/// The kernel manages `struct device_node` lifetimes through a
/// reference count: `of_node_get(node)`
/// increments it; `of_node_put(node)`
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
/// Motivating CVE family: Open Firmware / device tree refcount UAFs (55+ CVE descriptions mention of_node_put).
/// Subsystem focus: `drivers/of, drivers/*`.
///
/// ## Abstraction
///
/// `struct device_node` is left as a forward declaration in this
/// module's public header; the full kernel definition is unified
/// in at link time.  The property only ever uses pointer identity
/// against the ghost table — it never dereferences fields.

#ifndef INTEGRATION_LINUX_PROPERTIES_OF_NODE_LIFETIME_OF_NODE_LIFETIME_H
#define INTEGRATION_LINUX_PROPERTIES_OF_NODE_LIFETIME_OF_NODE_LIFETIME_H

#include <stddef.h>

// `struct device_node` is left opaque (see kobject_lifetime.h's
// note for rationale: structurally embedding a partial definition
// here would conflict with the kernel's full struct at link time
// and re-introduce LIM-016).  TUs that need a concrete instance
// supply their own definition; scan adapters get the kernel's
// full struct via the kernel TU at link time.
struct device_node;

// Ghost state API.
void of_node_lifetime_init(struct device_node *node,
                              unsigned int usage);
void of_node_lifetime_get(struct device_node *node);
void of_node_lifetime_put(struct device_node *node);
unsigned int of_node_lifetime_usage(struct device_node *node);

// Predicate: the object is live (refcount > 0, has not been
// freed).  Safe to call inside a `__CPROVER_requires` clause.
int of_node_live(struct device_node *node);

#endif
