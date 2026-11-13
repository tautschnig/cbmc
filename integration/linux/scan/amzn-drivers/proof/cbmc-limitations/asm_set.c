/* Minimal reproducer: does CBMC observe a write performed by inline asm? */
int main(void)
{
  unsigned long flags = 0;
  /* exactly the shape FreeBSD's ATOMIC_ASM generates for atomic_set_long */
  __asm__ __volatile__("lock; orq %1,%0"
                       : "+m"(flags)
                       : "ir"((unsigned long)0x4)
                       : "cc");
  __CPROVER_assert(flags == 0, "A: CBMC thinks the asm wrote nothing");
  __CPROVER_assert(flags == 0x4, "B: the asm set the bit");
  return 0;
}
