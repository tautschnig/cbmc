/// \file
/// copy_from_user_size_check_kernel_adapter.c

#include <stddef.h>

int copy_len_safe(size_t dst_capacity, size_t len);

void __assert_copy_safe(size_t dst_capacity, size_t len)
  __CPROVER_requires(copy_len_safe(dst_capacity, len) == 1)
  __CPROVER_assigns();
