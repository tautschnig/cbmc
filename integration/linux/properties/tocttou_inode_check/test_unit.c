/// \file
/// test_unit.c — sequential sanity tests for
/// tocttou_inode_check.

#include "tocttou_inode_check.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  tic_init(7);
  __CPROVER_assert(tic_check() == 7, "fresh init: 7");

  tic_change(42);
  __CPROVER_assert(tic_check() == 42, "after change: 42");

  // Sequential check-then-act with no intervening change:
  // the captured value still matches.
  unsigned int v = tic_check();
  tic_act_assuming(v); // contract precondition holds
  // (asserted here only because the unit test re-checks)
  __CPROVER_assert(tic_check() == v, "still equal");

  return 0;
}
