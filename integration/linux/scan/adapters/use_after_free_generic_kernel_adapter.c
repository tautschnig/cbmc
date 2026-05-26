/// \file
/// use_after_free_generic_kernel_adapter.c
///
/// __assert_not_freed(p) is a synthetic checkpoint
/// inserted by Coccinelle instrumentation before each
/// post-kfree x->field deref site.  Provides an empty
/// body so linking succeeds and goto-instrument can apply
/// `--replace-call-with-contract`.

int is_freed(const void *p);

void __assert_not_freed(const void *p) __CPROVER_requires(is_freed(p) == 0)
  __CPROVER_assigns()
{
  (void)p;
}
