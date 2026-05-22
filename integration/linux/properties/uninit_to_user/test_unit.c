/// \file
/// test_unit.c

#include "uninit_to_user.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  int a, b;
  void *pa = &a;
  void *pb = &b;

  __CPROVER_assert(is_initialised(pa) == 0, "fresh: not initialised");

  mark_initialised(pa);
  __CPROVER_assert(is_initialised(pa) == 1, "after mark: initialised");
  mark_uninitialised(pa);
  __CPROVER_assert(is_initialised(pa) == 0, "after unmark: not initialised");

  mark_initialised(pa);
  mark_initialised(pb);
  mark_uninitialised(pa);
  __CPROVER_assert(is_initialised(pa) == 0, "a uninit");
  __CPROVER_assert(is_initialised(pb) == 1, "b initialised");

  __CPROVER_assert(is_initialised((void *)0) == 0, "NULL: not initialised");

  return 0;
}
