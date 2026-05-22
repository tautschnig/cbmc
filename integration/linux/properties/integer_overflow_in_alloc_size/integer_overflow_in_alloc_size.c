/// \file
/// integer_overflow_in_alloc_size.c — reference impl.

#include "integer_overflow_in_alloc_size.h"

// Returns 1 iff n * elem_size doesn't overflow size_t.
// Equivalent to the kernel's check_mul_overflow (shorted to
// the size_t case).
int alloc_size_safe(size_t n, size_t elem_size)
{
  if(elem_size == 0u)
    return 1;
  if(n > ((size_t)-1) / elem_size)
    return 0;
  return 1;
}
