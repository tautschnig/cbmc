/* The Linux kernel's set_bit, same shape */
static inline void set_bit(int nr, volatile unsigned long *addr)
{
  __asm__ __volatile__("lock; bts %1,%0" : "+m"(*addr) : "Ir"(nr) : "memory");
}
int main(void)
{
  unsigned long word = 0;
  set_bit(3, &word);
  __CPROVER_assert(word == 0, "D: Linux set_bit also writes nothing");
  return 0;
}
