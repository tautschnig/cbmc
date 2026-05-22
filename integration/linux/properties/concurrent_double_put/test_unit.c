/// \file
/// test_unit.c — sequential sanity tests for
/// concurrent_double_put.

#include "concurrent_double_put.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  cdp_clear();
  __CPROVER_assert(cdp_is_live() == 0, "fresh: not live");

  cdp_get();
  __CPROVER_assert(cdp_is_live() == 1, "after get: live");

  cdp_put();
  __CPROVER_assert(cdp_is_live() == 0, "after put: dead");

  // Sequential second put: should be a no-op (the body of
  // cdp_put guards the decrement).
  cdp_put();
  __CPROVER_assert(cdp_is_live() == 0, "second put: still dead");

  cdp_get();
  cdp_get();
  // Reference impl uses a boolean live flag rather than a
  // refcount; second get is idempotent.
  __CPROVER_assert(cdp_is_live() == 1, "two gets: still live");

  return 0;
}
