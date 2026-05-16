/// \file
/// test_unit.c — unit tests for the kobject_lifetime property module.

#include "kobject_lifetime.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

// Concrete struct kobject for this abstract test.  The property
// module's public header forward-declares it; any TU that wants
// to stack-allocate a kobject sentinel provides its own layout
// (the ghost table only uses pointer identity).
struct kobject
{
  int dummy;
};

int main(void)
{
  struct kobject a, b, c;

  // test 1: fresh un-init kobject → not live (ghost defaults to 0).
  __CPROVER_assert(kobject_live(&a) == 0, "untracked kobject reports not live");

  // test 2: init with usage=1 → live; put once → not live.
  kobject_lifetime_init(&b, 1);
  __CPROVER_assert(kobject_live(&b) == 1, "usage=1 is live");
  kobject_lifetime_put(&b);
  __CPROVER_assert(kobject_live(&b) == 0, "after single put, not live");

  // test 3: init with usage=2 → live; get once → live; put once → live;
  //         put once → live (usage still 1); put once → not live.
  kobject_lifetime_init(&c, 2);
  kobject_lifetime_get(&c);
  __CPROVER_assert(kobject_live(&c) == 1, "usage=3 still live");
  kobject_lifetime_put(&c);
  __CPROVER_assert(kobject_live(&c) == 1, "usage=2 still live");
  kobject_lifetime_put(&c);
  __CPROVER_assert(kobject_live(&c) == 1, "usage=1 still live");
  kobject_lifetime_put(&c);
  __CPROVER_assert(kobject_live(&c) == 0, "usage=0 is dead");

  // test 4: NULL kobject → not live (defensive).
  __CPROVER_assert(kobject_live((struct kobject *)0) == 0, "NULL is not live");

  return 0;
}
