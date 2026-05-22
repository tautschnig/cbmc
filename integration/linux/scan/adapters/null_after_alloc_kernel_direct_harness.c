/// \file
/// null_after_alloc_kernel_direct_harness.c
///
/// Vulnerable: kmalloc returns nondet (could be NULL); harness
/// passes the result to the deref-safe checkpoint without first
/// confirming non-NULL.  CBMC finds an interleaving where the
/// allocator returned NULL and the precondition fires.
///
/// Fix shape (-DFIXED): explicitly guards the deref by either
/// returning early on NULL or marking the pointer as
/// null-check-done after testing it.

void assert_null_check_done(const void *p);
void assert_null_check_clear(const void *p);

void __assert_safe_to_deref(const void *p);

// Model an allocator that may return NULL.  CBMC treats reads
// from uninitialised stack locations as nondet, so we use that
// to model the allocator's success/failure choice.
static char buf[1024];

static const void *alloc_may_fail(void)
{
  // Uninit local -> CBMC picks nondeterministically.
  int choice;
  if(choice)
    return (const void *)0;
  return (const void *)buf;
}

int main(void)
{
  const void *p = alloc_may_fail();

#ifdef FIXED
  if(!p)
    return -1;
  // After the check, mark the pointer.  Harness explicitly
  // asserts the check happened before deref.
  assert_null_check_done(p);
#endif

  __assert_safe_to_deref(p);
  return 0;
}
