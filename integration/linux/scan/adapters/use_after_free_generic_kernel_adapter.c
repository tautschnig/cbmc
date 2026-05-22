/// \file
/// use_after_free_generic_kernel_adapter.c

int is_freed(const void *p);

void __assert_not_freed(const void *p) __CPROVER_requires(is_freed(p) == 0)
  __CPROVER_assigns();
