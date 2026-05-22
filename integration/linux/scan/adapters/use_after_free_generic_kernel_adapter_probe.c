/// \file
/// use_after_free_generic_kernel_adapter_probe.c

void __assert_not_freed(const void *p) __CPROVER_requires(0 == 1)
  __CPROVER_assigns();
