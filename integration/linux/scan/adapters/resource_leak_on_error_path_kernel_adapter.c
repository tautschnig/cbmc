/// \file
/// resource_leak_on_error_path_kernel_adapter.c
///
/// __assert_no_leak_at_exit(p) is a synthetic checkpoint
/// inserted by Coccinelle instrumentation.  This file
/// declares its CBMC contract AND provides an empty body
/// so that linking succeeds and goto-instrument can apply
/// `--replace-call-with-contract` to it.

int leak_outstanding(const void *p);

void __assert_no_leak_at_exit(const void *p)
  __CPROVER_requires(leak_outstanding(p) == 0) __CPROVER_assigns()
{
  (void)p;
}
