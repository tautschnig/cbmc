/// \file
/// test_unit.c — unit tests for the cred_lifetime property module.

#include "cred_lifetime.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

// Concrete struct cred for this abstract test.  The property
// module's public header forward-declares it (so scan adapters
// can link with the kernel's real definition); any TU that
// wants to stack-allocate a cred sentinel provides its own
// layout.  The field doesn't matter — the ghost table only
// uses the pointer identity.
struct cred
{
  int dummy;
};

int main(void)
{
  struct cred a, b, c;

  // test 1: fresh un-init cred → not live (ghost defaults to 0).
  __CPROVER_assert(cred_live(&a) == 0, "untracked cred reports not live");

  // test 2: init with usage=1 → live; put once → not live.
  cred_lifetime_init(&b, 1);
  __CPROVER_assert(cred_live(&b) == 1, "usage=1 is live");
  cred_lifetime_put(&b);
  __CPROVER_assert(cred_live(&b) == 0, "after single put, not live");

  // test 3: init with usage=2 → live; get once → live; put once → live;
  //         put once → live (usage still 1); put once → not live.
  cred_lifetime_init(&c, 2);
  cred_lifetime_get(&c);
  __CPROVER_assert(cred_live(&c) == 1, "usage=3 still live");
  cred_lifetime_put(&c);
  __CPROVER_assert(cred_live(&c) == 1, "usage=2 still live");
  cred_lifetime_put(&c);
  __CPROVER_assert(cred_live(&c) == 1, "usage=1 still live");
  cred_lifetime_put(&c);
  __CPROVER_assert(cred_live(&c) == 0, "usage=0 is dead");

  // test 4: NULL cred → not live (defensive).
  __CPROVER_assert(cred_live((struct cred *)0) == 0, "NULL is not live");

  return 0;
}
