/// \file
/// test_unit.c — unit tests for resource_leak_on_error_path.

#include "resource_leak_on_error_path.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  int a, b;
  void *pa = &a;
  void *pb = &b;

  __CPROVER_assert(leak_outstanding(pa) == 0, "fresh: not outstanding");

  leak_alloc_track(pa);
  __CPROVER_assert(leak_outstanding(pa) == 1, "after track: outstanding");
  leak_alloc_freed(pa);
  __CPROVER_assert(leak_outstanding(pa) == 0, "after freed: not outstanding");

  leak_alloc_track(pa);
  leak_alloc_track(pb);
  leak_alloc_freed(pa);
  __CPROVER_assert(leak_outstanding(pa) == 0, "a freed");
  __CPROVER_assert(leak_outstanding(pb) == 1, "b still outstanding");

  __CPROVER_assert(leak_outstanding((void *)0) == 0, "NULL: not outstanding");

  return 0;
}
