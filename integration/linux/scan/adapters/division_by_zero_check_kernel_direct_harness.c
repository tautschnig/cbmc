/// \file
/// division_by_zero_check_kernel_direct_harness.c

void __assert_divisor_safe(long long d);

int main(void)
{
  long long d;  // unbounded — could be zero

#ifndef FIXED
  // Vuln: pass unbounded d as the divisor without a guard.
  __assert_divisor_safe(d);
#else
  // Fix: guard before the division.
  if(d == 0)
    return -1;
  __assert_divisor_safe(d);
#endif

  return 0;
}
