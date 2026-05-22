/// \file
/// resource_leak_on_error_path_kernel_adapter.c

int leak_outstanding(const void *p);

void __assert_no_leak_at_exit(const void *p)
  __CPROVER_requires(leak_outstanding(p) == 0) __CPROVER_assigns();
