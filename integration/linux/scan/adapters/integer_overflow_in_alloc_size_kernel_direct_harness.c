/// \file
/// integer_overflow_in_alloc_size_kernel_direct_harness.c

#include <stddef.h>

void __assert_size_safe(size_t n, size_t elem_size);

int main(void)
{
#ifndef FIXED
  // Vuln: nondet n times sizeof(struct foo) (= 32 say).  CBMC's
  // symex picks a value of n; if n > SIZE_MAX/32 the multiplication
  // overflows.  The contract precondition fires.
  size_t n;
  __assert_size_safe(n, 32u);
#else
  // Fix: explicit guard reduces n to a safe range BEFORE the call.
  size_t n;
  if(n > ((size_t)-1) / 32u)
    return -1;
  __assert_size_safe(n, 32u);
#endif

  return 0;
}
