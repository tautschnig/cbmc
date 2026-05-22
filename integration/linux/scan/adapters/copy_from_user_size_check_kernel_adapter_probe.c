/// \file
/// copy_from_user_size_check_kernel_adapter_probe.c

#include <stddef.h>

void __assert_copy_safe(size_t dst_capacity, size_t len)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
