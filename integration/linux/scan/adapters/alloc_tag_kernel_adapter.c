/// \file
/// alloc_tag_kernel_adapter.c — attaches the alloc_tag property
/// module's `alloc_tag_vfree_ok` predicate as a contract
/// precondition on the kernel's `vfree` API.
///
/// ## Why vfree?
///
/// `vfree` is defined as an extern in <linux/vmalloc.h>
/// (not static inline), so a single external-name contract
/// attaches cleanly at every kernel call site.  The bug class
/// is `vfree(p)` where `p` came from `kmalloc` (or another
/// non-vmalloc family) — the contract's
/// `alloc_tag_vfree_ok(p) == 1` fires on mismatch.
///
/// ## The precondition
///
///     __CPROVER_requires(alloc_tag_vfree_ok(addr) == 1)
///
/// Translation: the ghost tag on `addr` must be one of
/// {UNKNOWN, VMALLOC, KVMALLOC}.  KMALLOC, KMEM_CACHE,
/// ALLOC_PAGES all fire.  NULL is accepted (the predicate
/// treats NULL as always-OK to match the kernel's vfree(NULL)
/// behaviour).

int alloc_tag_vfree_ok(void *p);

void vfree(const void *addr)
  __CPROVER_requires(alloc_tag_vfree_ok((void *)addr) == 1) __CPROVER_assigns();
