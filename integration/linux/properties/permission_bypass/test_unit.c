/// \file
/// test_unit.c — unit tests for permission_bypass.

#include "permission_bypass.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  cap_check_clear();
  __CPROVER_assert(cap_was_checked() == 0, "fresh: not checked");

  cap_check_passed();
  __CPROVER_assert(cap_was_checked() == 1, "after pass: checked");

  cap_check_clear();
  __CPROVER_assert(cap_was_checked() == 0, "after clear: not checked");

  return 0;
}
