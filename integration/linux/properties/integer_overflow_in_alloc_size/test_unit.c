/// \file
/// test_unit.c — unit tests for integer_overflow_in_alloc_size.

#include "integer_overflow_in_alloc_size.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  __CPROVER_assert(alloc_size_safe(10u, 4u) == 1, "10 * 4 = 40: safe");
  __CPROVER_assert(
    alloc_size_safe((size_t)-1, 1u) == 1,
    "SIZE_MAX * 1: safe (no overflow)");
  __CPROVER_assert(
    alloc_size_safe((size_t)-1, 2u) == 0,
    "SIZE_MAX * 2: overflow");
  __CPROVER_assert(
    alloc_size_safe(((size_t)-1) / 4u, 4u) == 1,
    "(MAX/4) * 4: safe (exact)");
  __CPROVER_assert(
    alloc_size_safe(((size_t)-1) / 4u + 1u, 4u) == 0,
    "(MAX/4 + 1) * 4: overflow");
  __CPROVER_assert(alloc_size_safe(0u, 1u) == 1, "0 * 1: safe");
  __CPROVER_assert(alloc_size_safe(100u, 0u) == 1, "100 * 0: safe");
  return 0;
}
