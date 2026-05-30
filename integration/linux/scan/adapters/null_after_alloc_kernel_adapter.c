/// \file
/// null_after_alloc_kernel_adapter.c — adapter contracts.
///
/// __assert_safe_to_deref(p) is a synthetic checkpoint
/// inserted by Coccinelle instrumentation before each
/// kmalloc-then-deref site.  Provides an empty body so
/// linking succeeds and goto-instrument can apply
/// `--replace-call-with-contract`.
///
/// Contract uses a single helper `__null_after_alloc_safe`
/// rather than an inline `p != NULL || null_check_done(p)`
/// expression.  The latter form was being mis-compiled by
/// `goto-instrument --replace-call-with-contract`: the
/// short-circuit IF guard for the OR was being elided,
/// producing an unconditional `tmp_if_expr := true` and
/// asserting trivially.  Verified on CVE-2024-43818
/// (st_es8336_late_probe) where the bug was masked by
/// this miscompile.

int null_check_done(const void *p);

int __null_after_alloc_safe(const void *p)
{
  if(p != (const void *)0)
    return 1;
  return null_check_done(p);
}

void __assert_safe_to_deref(const void *p)
  __CPROVER_requires(__null_after_alloc_safe(p) == 1)
    __CPROVER_assigns()
{
  (void)p;
}
