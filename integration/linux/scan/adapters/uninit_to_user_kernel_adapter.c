/// \file
/// uninit_to_user_kernel_adapter.c

int is_initialised(const void *p);

void __assert_safe_for_userspace(const void *p)
  __CPROVER_requires(is_initialised(p) == 1) __CPROVER_assigns();
