/// \file
/// test_unit.c — unit tests for the refcount_lifetime property module.

#include "refcount_lifetime.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

// Concrete refcount_t definition for this abstract test.  The
// property module's public header forward-declares it; any TU
// that wants to stack-allocate a counter sentinel provides its
// own layout.  The field doesn't matter — the ghost table only
// uses the pointer identity.
struct refcount_struct
{
  int counter;
};

int main(void)
{
  refcount_t a, b, c;

  // test 1: fresh un-init refcount → not live.
  __CPROVER_assert(
    refcount_live(&a) == 0, "untracked refcount reports not live");

  // test 2: init with usage=1 → live; dec_and_test once → reached zero,
  //         not live.
  refcount_lifetime_init(&b, 1);
  __CPROVER_assert(refcount_live(&b) == 1, "usage=1 is live");
  __CPROVER_assert(
    refcount_lifetime_dec_and_test(&b) == 1,
    "dec from 1 returns 1 (reached zero)");
  __CPROVER_assert(refcount_live(&b) == 0, "after dec_and_test, not live");

  // test 3: init with usage=3 → inc / dec_and_test pairs balance correctly.
  refcount_lifetime_init(&c, 3);
  refcount_lifetime_inc(&c);
  __CPROVER_assert(refcount_live(&c) == 1, "usage=4 still live");
  __CPROVER_assert(
    refcount_lifetime_dec_and_test(&c) == 0, "dec from 4 returns 0");
  __CPROVER_assert(refcount_live(&c) == 1, "usage=3 still live");
  __CPROVER_assert(
    refcount_lifetime_dec_and_test(&c) == 0, "dec from 3 returns 0");
  __CPROVER_assert(
    refcount_lifetime_dec_and_test(&c) == 0, "dec from 2 returns 0");
  __CPROVER_assert(
    refcount_lifetime_dec_and_test(&c) == 1,
    "dec from 1 returns 1 (reached zero)");
  __CPROVER_assert(refcount_live(&c) == 0, "usage=0 is dead");

  // test 4: NULL refcount → not live (defensive).
  __CPROVER_assert(refcount_live((refcount_t *)0) == 0, "NULL is not live");

  return 0;
}
