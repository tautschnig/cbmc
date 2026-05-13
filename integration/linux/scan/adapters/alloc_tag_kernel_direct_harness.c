/// \file
/// alloc_tag_kernel_direct_harness.c — direct-call harness for
/// the alloc_tag property module.
///
/// Tags a pointer as `ALLOC_TAG_KMALLOC` (vulnerable shape)
/// or `ALLOC_TAG_VMALLOC` (fix shape, `-DFIXED`), then calls
/// `vfree`.  The vulnerable shape fires the
/// `alloc_tag_vfree_ok` precondition because KMALLOC-tagged
/// pointers are not freeable by vfree.

typedef enum
{
  ALLOC_TAG_UNKNOWN = 0,
  ALLOC_TAG_KMALLOC,
  ALLOC_TAG_VMALLOC,
  ALLOC_TAG_KVMALLOC,
  ALLOC_TAG_KMEM_CACHE,
  ALLOC_TAG_ALLOC_PAGES,
} alloc_tag_t;

void alloc_tag_mark(void *p, alloc_tag_t tag);

void vfree(const void *addr);

int main(void)
{
  // Backing buffer for the sentinel pointer.  Content doesn't
  // matter; only the address is used as a ghost-table key.
  static char sentinel[64];
  void *p = sentinel;

#ifndef FIXED
  // Vulnerable: the pointer is tagged as having been allocated
  // by kmalloc, but the harness then calls vfree(p).  The
  // contract's alloc_tag_vfree_ok precondition fires.
  alloc_tag_mark(p, ALLOC_TAG_KMALLOC);
#else
  // Safe: the pointer is tagged as vmalloc-allocated; vfree
  // is the correct free API and the precondition holds.
  alloc_tag_mark(p, ALLOC_TAG_VMALLOC);
#endif

  vfree(p);
  return 0;
}
