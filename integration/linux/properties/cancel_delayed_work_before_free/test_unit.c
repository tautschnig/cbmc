/// \file
/// test_unit.c — unit tests for cancel_delayed_work_before_free.

#include "cancel_delayed_work_before_free.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

struct delayed_work
{
  int dummy;
};

int main(void)
{
  struct delayed_work a, b;

  __CPROVER_assert(cancel_dwork_pending(&a) == 0, "untracked: not pending");

  cancel_dwork_set_pending(&a);
  __CPROVER_assert(cancel_dwork_pending(&a) == 1, "after set: pending");
  cancel_dwork_clear_pending(&a);
  __CPROVER_assert(cancel_dwork_pending(&a) == 0, "after clear: not pending");

  cancel_dwork_set_pending(&a);
  cancel_dwork_set_pending(&b);
  cancel_dwork_clear_pending(&a);
  __CPROVER_assert(cancel_dwork_pending(&a) == 0, "a cleared, b unchanged");
  __CPROVER_assert(cancel_dwork_pending(&b) == 1, "b still pending");

  __CPROVER_assert(
    cancel_dwork_pending((struct delayed_work *)0) == 0,
    "NULL: not pending");

  return 0;
}
