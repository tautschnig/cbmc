/// \file
/// test_unit.c — unit tests for the lock_state property module.

#include "lock_state.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

// Concrete struct mutex for stack allocation in the unit test.
// The property module's header leaves `struct mutex` forward-
// declared; the kernel's real struct is linked in on the scan
// path.  Here we just need pointers with stable identity.
struct mutex
{
  int dummy;
};

int main(void)
{
  struct mutex a, b;

  // ---- test 1: fresh mutex reports not-held ----
  __CPROVER_assert(lock_held(&a) == 0, "fresh mutex not held");

  // ---- test 2: single lock → held ----
  lock_state_lock(&a);
  __CPROVER_assert(lock_held(&a) == 1, "after lock, held");
  lock_state_unlock_ghost(&a);
  __CPROVER_assert(lock_held(&a) == 0, "after unlock, not held");

  // ---- test 3: nested locks track a count, not just a bit ----
  lock_state_lock(&b);
  lock_state_lock(&b);
  __CPROVER_assert(lock_held(&b) == 1, "count=2 still reports held");
  lock_state_unlock_ghost(&b);
  __CPROVER_assert(lock_held(&b) == 1, "count=1 still held");
  lock_state_unlock_ghost(&b);
  __CPROVER_assert(lock_held(&b) == 0, "count=0 not held");

  // ---- test 4: extra unlock is a no-op on the ghost ----
  lock_state_unlock_ghost(&b);
  __CPROVER_assert(lock_held(&b) == 0, "over-unlock stays at 0");

  // ---- test 5: NULL → not held ----
  __CPROVER_assert(lock_held((struct mutex *)0) == 0, "NULL not held");

  return 0;
}
