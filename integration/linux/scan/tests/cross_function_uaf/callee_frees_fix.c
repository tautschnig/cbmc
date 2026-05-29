/// \file
/// callee_frees_fix.c — fixed counterpart to
/// callee_frees.c for the cross-function UAF PoC.
///
/// The fix reorders the deref to happen BEFORE the free.
/// CBMC should report VERIFICATION SUCCESSFUL.

extern void *kmalloc(unsigned long size, unsigned int gfp);
extern void kfree(const void *p);

extern void mark_freed(const void *p);
extern void __assert_not_freed(const void *p);

void callee_frees_fix(void *p)
{
  kfree(p);
  mark_freed(p);
}

int caller_uses_then_frees(void *p)
{
  __assert_not_freed(p);
  int v = *(int *)p;
  callee_frees_fix(p);
  return v;
}
