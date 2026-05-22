/// \file
/// null_after_alloc_kernel_adapter.c — adapter contracts.

int null_check_done(const void *p);

void __assert_safe_to_deref(const void *p)
  __CPROVER_requires(p != (const void *)0 || null_check_done(p) == 1)
    __CPROVER_assigns();
