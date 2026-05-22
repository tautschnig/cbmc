/// \file
/// division_by_zero_check_kernel_adapter_probe.c

void __assert_divisor_safe(long long d)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
