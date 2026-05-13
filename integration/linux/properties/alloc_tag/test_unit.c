/// \file
/// test_unit.c — unit tests for the alloc_tag property module.

#include "alloc_tag.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  char buf_a[64];
  char buf_b[64];
  char buf_c[64];

  void *a = buf_a;
  void *b = buf_b;
  void *c = buf_c;

  // test 1: un-tagged pointers report UNKNOWN, and both
  // kfree_ok and vfree_ok accept them (caller's responsibility).
  __CPROVER_assert(
    alloc_tag_of(a) == ALLOC_TAG_UNKNOWN, "untagged reports UNKNOWN");
  __CPROVER_assert(alloc_tag_kfree_ok(a) == 1, "UNKNOWN kfree ok");
  __CPROVER_assert(alloc_tag_vfree_ok(a) == 1, "UNKNOWN vfree ok");

  // test 2: tagged as kmalloc → kfree ok, vfree NOT ok.
  alloc_tag_mark(a, ALLOC_TAG_KMALLOC);
  __CPROVER_assert(alloc_tag_of(a) == ALLOC_TAG_KMALLOC, "kmalloc tag stored");
  __CPROVER_assert(alloc_tag_kfree_ok(a) == 1, "KMALLOC kfree ok");
  __CPROVER_assert(
    alloc_tag_vfree_ok(a) == 0, "KMALLOC vfree NOT ok (bug class)");

  // test 3: tagged as vmalloc → vfree ok, kfree NOT ok.
  alloc_tag_mark(b, ALLOC_TAG_VMALLOC);
  __CPROVER_assert(
    alloc_tag_kfree_ok(b) == 0, "VMALLOC kfree NOT ok (bug class)");
  __CPROVER_assert(alloc_tag_vfree_ok(b) == 1, "VMALLOC vfree ok");

  // test 4: tagged as kvmalloc → BOTH kfree and vfree are
  // accepted (in kernel, kvfree dispatches; so is kfree if the
  // kvmalloc fell back to kmalloc, though this is somewhat
  // defensive — the safer modelling is "both OK").
  alloc_tag_mark(c, ALLOC_TAG_KVMALLOC);
  __CPROVER_assert(alloc_tag_kfree_ok(c) == 1, "KVMALLOC kfree ok");
  __CPROVER_assert(alloc_tag_vfree_ok(c) == 1, "KVMALLOC vfree ok");

  // test 5: after clear() a previously-tagged pointer is
  // UNKNOWN again (simulating what the ghost does when the
  // tagged memory is freed and reused for a new allocation
  // with a different family).
  alloc_tag_clear(a);
  __CPROVER_assert(
    alloc_tag_of(a) == ALLOC_TAG_UNKNOWN, "cleared back to UNKNOWN");

  // test 6: NULL pointer is always OK for both frees.
  __CPROVER_assert(alloc_tag_kfree_ok((void *)0) == 1, "NULL kfree ok");
  __CPROVER_assert(alloc_tag_vfree_ok((void *)0) == 1, "NULL vfree ok");

  return 0;
}
