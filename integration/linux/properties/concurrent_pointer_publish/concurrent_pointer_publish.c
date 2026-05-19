/// \file
/// concurrent_pointer_publish.c — reference implementation.

#include "concurrent_pointer_publish.h"

unsigned int __cpp_published = 0;
unsigned int __cpp_initialised = 0;

void cpp_publish(void)
{
  __cpp_published = 1;
}

void cpp_initialise(void)
{
  __cpp_initialised = 1;
}

void cpp_clear(void)
{
  __cpp_published = 0;
  __cpp_initialised = 0;
}

int cpp_is_published(void)
{
  return __cpp_published != 0u ? 1 : 0;
}

int cpp_is_initialised(void)
{
  return __cpp_initialised != 0u ? 1 : 0;
}

int cpp_safe_to_read(void)
{
  // Safe iff the publish bit implies the init bit.
  // i.e.  !published OR initialised.
  return (__cpp_published == 0u || __cpp_initialised != 0u) ? 1 : 0;
}
