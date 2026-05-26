/// \file
/// null_after_alloc_kernel_adapter.c — adapter contracts.
///
/// __assert_safe_to_deref(p) is a synthetic checkpoint
/// inserted by Coccinelle instrumentation before each
/// kmalloc-then-deref site.  Provides an empty body so
/// linking succeeds and goto-instrument can apply
/// `--replace-call-with-contract`.

int null_check_done(const void *p);

void __assert_safe_to_deref(const void *p)
  __CPROVER_requires(p != (const void *)0 || null_check_done(p) == 1)
    __CPROVER_assigns()
{
  (void)p;
}
