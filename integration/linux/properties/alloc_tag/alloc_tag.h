/// \file
/// alloc_tag.h — property module for Linux kernel allocator-
/// mismatch bugs: `vfree(p)` where p came from `kmalloc`, or
/// `kfree(p)` where p came from `vmalloc`.
///
/// ## Bug class
///
/// The kernel offers multiple allocator families with distinct
/// free paths:
///
///     p = kmalloc(size, flags);    → must be freed by kfree
///     p = vmalloc(size);           → must be freed by vfree
///     p = kvmalloc(size, flags);   → may be freed by kvfree
///                                    (which dispatches to the
///                                    right backend)
///     p = kmem_cache_alloc(...);   → must be freed by
///                                    kmem_cache_free
///
/// Mismatching the alloc/free pair corrupts the heap:
/// `vfree` on a `kmalloc` pointer walks the vmalloc page table
/// for a pointer that is not in it; `kfree` on a `vmalloc`
/// pointer treats a per-page allocation as a slab object.
/// Both are exploitable, and both show up in the historical
/// kernel CVE record.
///
/// ## Property
///
/// Genuinely new semantic class relative to the existing
/// refcount / lock / structural-invariant modules: per-pointer
/// discrete TAG, not a counter.  Each allocator-call site
/// stamps its returned pointer with a tag; each free-call site
/// checks the tag against what it's allowed to free.  A
/// mismatch fires the contract.
///
/// ## Ghost state
///
/// The module keeps a per-pointer map `void * → alloc_tag_t`.
/// An unknown pointer (not in the map) is reported as
/// `ALLOC_TAG_UNKNOWN`, which the contracts treat as "caller
/// takes responsibility" to avoid false positives on
/// externally-allocated buffers.

#ifndef INTEGRATION_LINUX_PROPERTIES_ALLOC_TAG_ALLOC_TAG_H
#define INTEGRATION_LINUX_PROPERTIES_ALLOC_TAG_ALLOC_TAG_H

#include <stddef.h>

typedef enum
{
  ALLOC_TAG_UNKNOWN = 0,
  ALLOC_TAG_KMALLOC,
  ALLOC_TAG_VMALLOC,
  ALLOC_TAG_KVMALLOC,
  ALLOC_TAG_KMEM_CACHE,
  ALLOC_TAG_ALLOC_PAGES,
} alloc_tag_t;

// Ghost-state API: called from each allocator wrapper (or from
// a harness) to stamp a pointer with its allocator family, and
// cleared on free.
void alloc_tag_mark(void *p, alloc_tag_t tag);
void alloc_tag_clear(void *p);
alloc_tag_t alloc_tag_of(void *p);

// Predicate: is `p`'s tag one that may legally be freed by
// `kfree`?  That is: ALLOC_TAG_KMALLOC, ALLOC_TAG_KVMALLOC
// (kvfree/kfree dispatch on the actual backend), or
// ALLOC_TAG_UNKNOWN (untracked — caller's responsibility).
int alloc_tag_kfree_ok(void *p);

// Predicate: is `p`'s tag one that may legally be freed by
// `vfree`?  That is: ALLOC_TAG_VMALLOC, ALLOC_TAG_KVMALLOC, or
// ALLOC_TAG_UNKNOWN.  Note that `ALLOC_TAG_KMALLOC` is NOT OK
// here — that's the bug class we're trying to catch.
int alloc_tag_vfree_ok(void *p);

// Predicate: NULL is always OK to pass to kfree / vfree.  Both
// kernel APIs accept NULL and return without doing anything.
int alloc_tag_free_ok_null_or(void *p, int ok_when_tracked);

#endif
