/// \file
/// sock_lifetime.h — property module for Linux kernel
/// `struct sock` refcount / use-after-put bugs.
///
/// ## Bug class
///
/// The kernel manages `struct sock` lifetimes through a
/// reference count: `sock_hold(sk)`
/// increments it; `sock_put(sk)`
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
/// Motivating CVE family: AF_VSOCK / netlink / Bluetooth socket UAFs (24+ CVE descriptions mention sock_put, 18+ mention sock_hold).
/// Subsystem focus: `net/*`.
///
/// ## Abstraction
///
/// `struct sock` is left as a forward declaration in this
/// module's public header; the full kernel definition is unified
/// in at link time.  The property only ever uses pointer identity
/// against the ghost table — it never dereferences fields.

#ifndef INTEGRATION_LINUX_PROPERTIES_SOCK_LIFETIME_SOCK_LIFETIME_H
#define INTEGRATION_LINUX_PROPERTIES_SOCK_LIFETIME_SOCK_LIFETIME_H

#include <stddef.h>

// `struct sock` is left opaque (see kobject_lifetime.h's
// note for rationale: structurally embedding a partial definition
// here would conflict with the kernel's full struct at link time
// and re-introduce LIM-016).  TUs that need a concrete instance
// supply their own definition; scan adapters get the kernel's
// full struct via the kernel TU at link time.
struct sock;

// Ghost state API.
void sock_lifetime_init(struct sock *sk,
                              unsigned int usage);
void sock_lifetime_get(struct sock *sk);
void sock_lifetime_put(struct sock *sk);
unsigned int sock_lifetime_usage(struct sock *sk);

// Predicate: the object is live (refcount > 0, has not been
// freed).  Safe to call inside a `__CPROVER_requires` clause.
int sock_live(struct sock *sk);

#endif
