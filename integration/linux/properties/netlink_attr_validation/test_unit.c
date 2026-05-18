/// \file
/// test_unit.c — unit tests for netlink_attr_validation.

#include "netlink_attr_validation.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

struct nlattr
{
  unsigned short nla_len;
  unsigned short nla_type;
};

int main(void)
{
  struct nlattr a, b;

  // test 1: untracked -> validated size 0 -> not at least 1.
  __CPROVER_assert(nla_validated_size(&a) == 0, "untracked: size 0");
  __CPROVER_assert(nla_size_at_least(&a, 1) == 0, "untracked: not at least 1");
  __CPROVER_assert(nla_size_at_least(&a, 4) == 0, "untracked: not at least 4");

  // test 2: validate to size 4 -> at least 1, 2, 4 ok; not 8.
  nla_validate_min_size(&a, 4);
  __CPROVER_assert(nla_validated_size(&a) == 4, "after validate(4): size 4");
  __CPROVER_assert(nla_size_at_least(&a, 1) == 1, "validate(4) >= 1");
  __CPROVER_assert(nla_size_at_least(&a, 4) == 1, "validate(4) >= 4");
  __CPROVER_assert(nla_size_at_least(&a, 8) == 0, "validate(4) NOT >= 8");

  // test 3: raising to a smaller size is a no-op.
  nla_validate_min_size(&a, 2);
  __CPROVER_assert(nla_validated_size(&a) == 4, "raise(2) preserves earlier 4");

  // test 4: raising to a larger size updates.
  nla_validate_min_size(&a, 8);
  __CPROVER_assert(nla_validated_size(&a) == 8, "raise(8) updates");

  // test 5: clear -> size 0.
  nla_validate_clear(&a);
  __CPROVER_assert(nla_validated_size(&a) == 0, "after clear: size 0");

  // test 6: independent entries.
  nla_validate_min_size(&b, 4);
  nla_validate_min_size(&a, 8);
  __CPROVER_assert(nla_validated_size(&b) == 4, "b unchanged");

  // test 7: NULL -> not at least 1.
  __CPROVER_assert(
    nla_size_at_least((struct nlattr *)0, 1) == 0, "NULL: not at least 1");

  return 0;
}
