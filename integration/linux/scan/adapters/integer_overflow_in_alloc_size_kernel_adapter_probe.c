/// \file
/// integer_overflow_in_alloc_size_kernel_adapter_probe.c

#include <stddef.h>

void __assert_size_safe(size_t n, size_t elem_size)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
