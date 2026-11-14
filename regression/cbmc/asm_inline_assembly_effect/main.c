// The defect report: CBMC assumed inline assembly had no effect, so a write
// performed by an asm statement was invisible and a false property held.
// These statements are now interpreted, so the writes are observed.

int main(void)
{
  unsigned long flags = 0;
  // the shape FreeBSD's ATOMIC_ASM generates for atomic_set_long
  __asm__ __volatile__("lock; orq %1,%0"
                       : "+m"(flags)
                       : "ir"((unsigned long)0x4)
                       : "cc");
  // the write is now observed: the bit is set
  __CPROVER_assert(flags == 0x4, "the asm set the bit");
  // and the old, false, reading does not hold any more
  __CPROVER_assert(flags == 0, "SHOULD FAIL: asm wrote nothing");

  // the Linux set_bit shape
  unsigned long word = 0;
  __asm__ __volatile__("lock; bts %1,%0" : "+m"(word) : "Ir"(3) : "memory");
  __CPROVER_assert(word == 8, "set_bit set bit 3");

  return 0;
}
