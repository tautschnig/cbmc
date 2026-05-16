/// \file
/// test_unit.c — unit tests for the fput_lifetime property
/// module.

#include "fput_lifetime.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

// Concrete `struct file` for this abstract test.  The
// property module's public header forward-declares it; any TU
// that wants to stack-allocate a sentinel provides its own
// layout.  The field doesn't matter — the ghost table only
// uses pointer identity.
struct file
{
  int dummy;
};

int main(void)
{
  struct file a, b, c;

  // test 1: fresh un-init -> not live (ghost defaults to 0).
  __CPROVER_assert(
    fput_live(&a) == 0,
    "untracked reports not live");

  // test 2: init with usage=1 -> live; put once -> not live.
  fput_lifetime_init(&b, 1);
  __CPROVER_assert(fput_live(&b) == 1, "usage=1 is live");
  fput_lifetime_put(&b);
  __CPROVER_assert(
    fput_live(&b) == 0, "after single put, not live");

  // test 3: init with usage=2 -> live; get -> live; put thrice
  //         brings usage from 3 down to 0.
  fput_lifetime_init(&c, 2);
  fput_lifetime_get(&c);
  __CPROVER_assert(fput_live(&c) == 1, "usage=3 still live");
  fput_lifetime_put(&c);
  __CPROVER_assert(fput_live(&c) == 1, "usage=2 still live");
  fput_lifetime_put(&c);
  __CPROVER_assert(fput_live(&c) == 1, "usage=1 still live");
  fput_lifetime_put(&c);
  __CPROVER_assert(fput_live(&c) == 0, "usage=0 is dead");

  // test 4: NULL -> not live (defensive).
  __CPROVER_assert(
    fput_live((struct file *)0) == 0,
    "NULL is not live");

  return 0;
}
