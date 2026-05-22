/// \file
/// concurrent_double_put.c — reference implementation.

#include "concurrent_double_put.h"

unsigned int __cdp_live = 0;

void cdp_get(void)
{
  __cdp_live = 1;
}

void cdp_put(void)
{
  if(__cdp_live > 0u)
    __cdp_live = 0;
}

void cdp_clear(void)
{
  __cdp_live = 0;
}

int cdp_is_live(void)
{
  return __cdp_live != 0u ? 1 : 0;
}
