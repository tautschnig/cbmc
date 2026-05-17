/// \file
/// test_unit.c — unit tests for cancel_work_before_free.

#include "cancel_work_before_free.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

struct work_struct
{
  int dummy;
};

int main(void)
{
  struct work_struct a, b;

  // test 1: untracked -> not pending.
  __CPROVER_assert(cancel_work_pending(&a) == 0, "untracked: not pending");

  // test 2: set pending, then clear.
  cancel_work_set_pending(&a);
  __CPROVER_assert(cancel_work_pending(&a) == 1, "after set: pending");
  cancel_work_clear_pending(&a);
  __CPROVER_assert(cancel_work_pending(&a) == 0, "after clear: not pending");

  // test 3: independent entries.
  cancel_work_set_pending(&a);
  cancel_work_set_pending(&b);
  cancel_work_clear_pending(&a);
  __CPROVER_assert(cancel_work_pending(&a) == 0, "a cleared, b unchanged");
  __CPROVER_assert(cancel_work_pending(&b) == 1, "b still pending");

  // test 4: NULL -> not pending.
  __CPROVER_assert(
    cancel_work_pending((struct work_struct *)0) == 0,
    "NULL: not pending");

  return 0;
}
