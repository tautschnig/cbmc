/// \file
/// test_unit.c

#include "division_by_zero_check.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  __CPROVER_assert(divisor_safe(0) == 0, "0 is unsafe");
  __CPROVER_assert(divisor_safe(1) == 1, "1 is safe");
  __CPROVER_assert(divisor_safe(-1) == 1, "-1 is safe");
  __CPROVER_assert(
    divisor_safe((long long)0x7fffffffffffffffLL) == 1, "MAX safe");
  __CPROVER_assert(
    divisor_safe((long long)-0x8000000000000000LL) == 1, "MIN safe");
  return 0;
}
