/// \file
/// skb_lifetime.h — property module for Linux kernel
/// `struct sk_buff` refcount / use-after-put bugs.
///
/// ## Bug class
///
/// The kernel manages `struct sk_buff` lifetimes through a
/// reference count: `skb_get(skb)`
/// increments it; `kfree_skb(skb)`
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
/// Motivating CVE family: sk_buff UAFs in network stacks and drivers/net (16+ mention skb_put, 12+ mention skb_get).
/// Subsystem focus: `net/*, drivers/net/*`.
///
/// ## Abstraction
///
/// `struct sk_buff` is left as a forward declaration in this
/// module's public header; the full kernel definition is unified
/// in at link time.  The property only ever uses pointer identity
/// against the ghost table — it never dereferences fields.

#ifndef INTEGRATION_LINUX_PROPERTIES_SKB_LIFETIME_SKB_LIFETIME_H
#define INTEGRATION_LINUX_PROPERTIES_SKB_LIFETIME_SKB_LIFETIME_H

#include <stddef.h>

// `struct sk_buff` is left opaque (see kobject_lifetime.h's
// note for rationale: structurally embedding a partial definition
// here would conflict with the kernel's full struct at link time
// and re-introduce LIM-016).  TUs that need a concrete instance
// supply their own definition; scan adapters get the kernel's
// full struct via the kernel TU at link time.
struct sk_buff;

// Ghost state API.
void skb_lifetime_init(struct sk_buff *skb,
                              unsigned int usage);
void skb_lifetime_get(struct sk_buff *skb);
void skb_lifetime_put(struct sk_buff *skb);
unsigned int skb_lifetime_usage(struct sk_buff *skb);

// Predicate: the object is live (refcount > 0, has not been
// freed).  Safe to call inside a `__CPROVER_requires` clause.
int skb_live(struct sk_buff *skb);

#endif
