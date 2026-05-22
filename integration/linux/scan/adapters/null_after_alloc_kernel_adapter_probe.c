/// \file
/// null_after_alloc_kernel_adapter_probe.c

void __assert_safe_to_deref(const void *p) __CPROVER_requires(0 == 1)
  __CPROVER_assigns();
