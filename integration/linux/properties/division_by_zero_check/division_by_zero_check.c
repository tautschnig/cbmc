/// \file
/// division_by_zero_check.c — reference impl.

#include "division_by_zero_check.h"

int divisor_safe(long long d)
{
  return d != 0 ? 1 : 0;
}
