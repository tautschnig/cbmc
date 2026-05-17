/// \file
/// test_unit.c — unit tests for the rcu_critical_section
/// property module.

#include "rcu_critical_section.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  // test 1: fresh state -> depth 0, outside CS.
  __CPROVER_assert(rcu_csection_depth() == 0, "fresh: depth 0");
  __CPROVER_assert(rcu_outside_csection() == 1, "fresh: outside");
  __CPROVER_assert(rcu_in_csection() == 0, "fresh: not in");

  // test 2: enter -> depth 1, in CS.
  rcu_csection_enter();
  __CPROVER_assert(rcu_csection_depth() == 1, "after enter: depth 1");
  __CPROVER_assert(rcu_in_csection() == 1, "after enter: in CS");
  __CPROVER_assert(rcu_outside_csection() == 0, "after enter: not outside");

  // test 3: nested enter/leave balances.
  rcu_csection_enter();
  __CPROVER_assert(rcu_csection_depth() == 2, "after second enter: depth 2");
  rcu_csection_leave();
  __CPROVER_assert(rcu_csection_depth() == 1, "after leave: depth 1");
  rcu_csection_leave();
  __CPROVER_assert(rcu_csection_depth() == 0, "after second leave: depth 0");

  // test 4: leave below zero is a no-op (defensive).
  rcu_csection_leave();
  __CPROVER_assert(rcu_csection_depth() == 0, "leave at zero stays zero");

  return 0;
}
