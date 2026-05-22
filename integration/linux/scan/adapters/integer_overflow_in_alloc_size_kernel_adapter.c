/// \file
/// integer_overflow_in_alloc_size_kernel_adapter.c

#include <stddef.h>

int alloc_size_safe(size_t n, size_t elem_size);

void __assert_size_safe(size_t n, size_t elem_size)
  __CPROVER_requires(alloc_size_safe(n, elem_size) == 1)
  __CPROVER_assigns();
