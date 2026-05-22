/// \file
/// test_unit.c — unit tests.

#include "use_after_free_generic.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  int a, b;
  void *pa = &a;
  void *pb = &b;

  __CPROVER_assert(is_freed(pa) == 0, "fresh: not freed");

  mark_freed(pa);
  __CPROVER_assert(is_freed(pa) == 1, "after mark_freed: freed");
  mark_alive(pa);
  __CPROVER_assert(is_freed(pa) == 0, "after mark_alive: not freed");

  mark_freed(pa);
  mark_freed(pb);
  __CPROVER_assert(is_freed(pa) == 1, "a freed");
  __CPROVER_assert(is_freed(pb) == 1, "b freed");

  __CPROVER_assert(is_freed((void *)0) == 0, "NULL: not freed");

  return 0;
}
