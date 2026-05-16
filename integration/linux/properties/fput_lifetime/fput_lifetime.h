/// \file
/// fput_lifetime.h — property module for Linux kernel
/// `struct file` refcount / use-after-put bugs.
///
/// ## Bug class
///
/// The kernel manages `struct file` lifetimes through a
/// reference count: `get_file(file)`
/// increments it; `fput(file)`
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
/// Motivating CVE family: file struct UAFs / fdput races (34+ CVE descriptions mention fput).
/// Subsystem focus: `fs/*`.
///
/// ## Abstraction
///
/// `struct file` is left as a forward declaration in this
/// module's public header; the full kernel definition is unified
/// in at link time.  The property only ever uses pointer identity
/// against the ghost table — it never dereferences fields.

#ifndef INTEGRATION_LINUX_PROPERTIES_FPUT_LIFETIME_FPUT_LIFETIME_H
#define INTEGRATION_LINUX_PROPERTIES_FPUT_LIFETIME_FPUT_LIFETIME_H

#include <stddef.h>

// `struct file` is left opaque (see kobject_lifetime.h's
// note for rationale: structurally embedding a partial definition
// here would conflict with the kernel's full struct at link time
// and re-introduce LIM-016).  TUs that need a concrete instance
// supply their own definition; scan adapters get the kernel's
// full struct via the kernel TU at link time.
struct file;

// Ghost state API.
void fput_lifetime_init(struct file *file,
                              unsigned int usage);
void fput_lifetime_get(struct file *file);
void fput_lifetime_put(struct file *file);
unsigned int fput_lifetime_usage(struct file *file);

// Predicate: the object is live (refcount > 0, has not been
// freed).  Safe to call inside a `__CPROVER_requires` clause.
int fput_live(struct file *file);

#endif
