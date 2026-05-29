/// \file
/// callee_frees.c — POC test for cross-function UAF.
///
/// Two functions: callee_frees(p) calls kfree(p), and
/// caller_uses_after_free(p) calls callee_frees(p) and
/// then dereferences p.  This is a use-after-free that
/// the per-function scan cannot detect without
/// cross-function ghost-state propagation.
///
/// Expected behaviour: when both functions are
/// instrumented (uaf_track_freed inside callee_frees,
/// __assert_not_freed at the caller's deref), CBMC
/// flags the bug.

extern void *kmalloc(unsigned long size, unsigned int gfp);
extern void kfree(const void *p);

/* Cocci-instrumentation hooks (declared in the
 * use_after_free_generic adapter). */
extern void mark_freed(const void *p);
extern void __assert_not_freed(const void *p);

void callee_frees(void *p)
{
  kfree(p);
  mark_freed(p);
}

int caller_uses_after_free(void *p)
{
  callee_frees(p);
  __assert_not_freed(p);
  return *(int *)p;
}
