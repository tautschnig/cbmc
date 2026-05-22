/// \file
/// test_unit.c — unit tests for del_timer_sync_before_free.

#include "del_timer_sync_before_free.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

struct timer_list
{
  int dummy;
};

int main(void)
{
  struct timer_list a, b;

  __CPROVER_assert(timer_armed(&a) == 0, "untracked: not armed");

  timer_set_armed(&a);
  __CPROVER_assert(timer_armed(&a) == 1, "after set: armed");
  timer_clear_armed(&a);
  __CPROVER_assert(timer_armed(&a) == 0, "after clear: not armed");

  timer_set_armed(&a);
  timer_set_armed(&b);
  timer_clear_armed(&a);
  __CPROVER_assert(timer_armed(&a) == 0, "a cleared, b unchanged");
  __CPROVER_assert(timer_armed(&b) == 1, "b still armed");

  __CPROVER_assert(
    timer_armed((struct timer_list *)0) == 0, "NULL: not armed");

  return 0;
}
