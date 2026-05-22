/// \file
/// permission_bypass.c — reference impl.

#include "permission_bypass.h"

int __cap_checked = 0;

void cap_check_passed(void)
{
  __cap_checked = 1;
}

void cap_check_clear(void)
{
  __cap_checked = 0;
}

int cap_was_checked(void)
{
  return __cap_checked != 0 ? 1 : 0;
}
