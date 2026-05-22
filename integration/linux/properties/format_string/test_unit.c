/// \file
/// test_unit.c

#include "format_string.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  const char *fmt_const = "static format string";
  const char *fmt_user = "%s%n attacker controlled";

  __CPROVER_assert(format_is_constant(fmt_const) == 0, "fresh: not constant");

  mark_format_constant(fmt_const);
  __CPROVER_assert(format_is_constant(fmt_const) == 1, "after mark: constant");

  mark_format_tainted(fmt_user);
  __CPROVER_assert(format_is_constant(fmt_user) == 0, "after taint: not constant");

  __CPROVER_assert(format_is_constant((const char *)0) == 0, "NULL: not constant");

  return 0;
}
