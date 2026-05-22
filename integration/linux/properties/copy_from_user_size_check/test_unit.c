/// \file
/// test_unit.c — unit tests for copy_from_user_size_check.

#include "copy_from_user_size_check.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  __CPROVER_assert(copy_len_safe(64u, 32u) == 1, "32 <= 64: safe");
  __CPROVER_assert(copy_len_safe(64u, 64u) == 1, "64 <= 64: safe");
  __CPROVER_assert(copy_len_safe(64u, 65u) == 0, "65 > 64: unsafe");
  __CPROVER_assert(copy_len_safe(0u, 0u) == 1, "0 <= 0: safe");
  __CPROVER_assert(
    copy_len_safe(0u, 1u) == 0, "1 > 0: unsafe");
  __CPROVER_assert(
    copy_len_safe((size_t)-1, (size_t)-1) == 1, "MAX <= MAX: safe");

  return 0;
}
