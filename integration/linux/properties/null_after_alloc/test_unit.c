/// \file
/// test_unit.c — unit tests for null_after_alloc.

#include "null_after_alloc.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  int a, b;
  void *pa = &a;
  void *pb = &b;

  __CPROVER_assert(null_check_done(pa) == 0, "fresh: not checked");

  assert_null_check_done(pa);
  __CPROVER_assert(null_check_done(pa) == 1, "after mark: checked");
  assert_null_check_clear(pa);
  __CPROVER_assert(null_check_done(pa) == 0, "after clear: not checked");

  assert_null_check_done(pa);
  assert_null_check_done(pb);
  assert_null_check_clear(pa);
  __CPROVER_assert(null_check_done(pa) == 0, "a cleared, b unchanged");
  __CPROVER_assert(null_check_done(pb) == 1, "b still checked");

  __CPROVER_assert(null_check_done((void *)0) == 0, "NULL: not checked");

  return 0;
}
