/// \file
/// division_by_zero_check_kernel_adapter.c

int divisor_safe(long long d);

void __assert_divisor_safe(long long d)
  __CPROVER_requires(divisor_safe(d) == 1) __CPROVER_assigns();
