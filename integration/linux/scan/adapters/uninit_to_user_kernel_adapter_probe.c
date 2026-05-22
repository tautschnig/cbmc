/// \file
/// uninit_to_user_kernel_adapter_probe.c

void __assert_safe_for_userspace(const void *p) __CPROVER_requires(0 == 1)
  __CPROVER_assigns();
