/// \file
/// resource_leak_on_error_path_kernel_adapter.c
///
/// __assert_no_leak_at_exit(p) is the original per-pointer
/// checkpoint inserted by the cocci instrumenter (one
/// assertion per (return × tracked variable)).
///
/// __assert_no_outstanding_leak() is the per-return-only
/// variant inserted by the per-return fallback in
/// instrument-fallback.py — it checks the GLOBAL ghost
/// table for any outstanding allocation, reducing the
/// assertion fan-out from O(returns × variables) to
/// O(returns).  This keeps the symex budget bounded on
/// many-allocations functions.
///
/// Both contracts are declared with empty bodies so that
/// linking succeeds and goto-instrument can apply
/// `--replace-call-with-contract` to either or both.

int leak_outstanding(const void *p);
int leak_any_outstanding(void);

void __assert_no_leak_at_exit(const void *p)
  __CPROVER_requires(leak_outstanding(p) == 0) __CPROVER_assigns()
{
  (void)p;
}

void __assert_no_outstanding_leak(void)
  __CPROVER_requires(leak_any_outstanding() == 0) __CPROVER_assigns()
{
}
