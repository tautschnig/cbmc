/// \file
/// cross_uaf_struct_fix.c — fixed counterpart: write
/// p->v BEFORE calling the freeing helper.

struct foo { int v; };

extern void *kmalloc(unsigned long size, unsigned int gfp);
extern void kfree(const void *p);

void callee_frees_struct_fix(struct foo *p)
{
  kfree(p);
}

int caller_uses_then_frees_struct(struct foo *p)
{
  p->v = 42;     /* OK: use before free */
  int v = p->v;
  callee_frees_struct_fix(p);
  return v;
}
