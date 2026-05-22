/// \file
/// resource_leak_on_error_path_kernel_adapter_probe.c

void __assert_no_leak_at_exit(const void *p) __CPROVER_requires(0 == 1)
  __CPROVER_assigns();
