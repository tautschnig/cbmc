/* Does a "memory" clobber at least make CBMC forget what it knew? */
int main(void)
{
  unsigned long v = 7;
  unsigned long *p = &v;
  __asm__ __volatile__("" ::: "memory");
  __CPROVER_assert(v == 7, "C: value survives a memory clobber unchanged");
  (void)p;
  return 0;
}
