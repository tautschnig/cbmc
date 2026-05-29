/// \file
/// cross_uaf_struct.c — Phase 2 PoC vuln: cocci-driven
/// instrumentation should detect this without manual
/// __assert_not_freed/mark_freed calls.
///
/// callee_frees_struct frees its struct argument.
/// caller_uses_after_free_struct calls callee_frees_struct
/// then dereferences the freed memory.
///
/// Cocci's Phase 2 (mark_freed after kfree) plus Phase 1
/// (__assert_not_freed before x->fld assignment) together
/// catch this when both functions are linked into the same
/// goto binary (because the property module's ghost table
/// is shared).

struct foo { int v; };

extern void *kmalloc(unsigned long size, unsigned int gfp);
extern void kfree(const void *p);

void callee_frees_struct(struct foo *p)
{
  kfree(p);
}

int caller_uses_after_free_struct(struct foo *p)
{
  callee_frees_struct(p);
  p->v = 42;     /* BUG: use after free */
  return p->v;
}
