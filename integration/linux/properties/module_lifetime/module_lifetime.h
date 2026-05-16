/// \file
/// module_lifetime.h — property module for Linux kernel
/// `struct module` refcount / use-after-put bugs.
///
/// ## Bug class
///
/// The kernel manages `struct module` lifetimes through a
/// reference count: `try_module_get(module)`
/// increments it; `module_put(module)`
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
/// Motivating CVE family: module reference leaks blocking module unload (11+ CVE descriptions mention try_module_get).
/// Subsystem focus: `kernel/, drivers/*`.
///
/// ## Abstraction
///
/// `struct module` is left as a forward declaration in this
/// module's public header; the full kernel definition is unified
/// in at link time.  The property only ever uses pointer identity
/// against the ghost table — it never dereferences fields.

#ifndef INTEGRATION_LINUX_PROPERTIES_MODULE_LIFETIME_MODULE_LIFETIME_H
#define INTEGRATION_LINUX_PROPERTIES_MODULE_LIFETIME_MODULE_LIFETIME_H

#include <stddef.h>

// `struct module` is left opaque (see kobject_lifetime.h's
// note for rationale: structurally embedding a partial definition
// here would conflict with the kernel's full struct at link time
// and re-introduce LIM-016).  TUs that need a concrete instance
// supply their own definition; scan adapters get the kernel's
// full struct via the kernel TU at link time.
struct module;

// Ghost state API.
void module_lifetime_init(struct module *module,
                              unsigned int usage);
void module_lifetime_get(struct module *module);
void module_lifetime_put(struct module *module);
unsigned int module_lifetime_usage(struct module *module);

// Predicate: the object is live (refcount > 0, has not been
// freed).  Safe to call inside a `__CPROVER_requires` clause.
int module_live(struct module *module);

#endif
