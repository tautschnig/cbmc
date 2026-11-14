// An asm statement that is not understood is approximated conservatively: its
// outputs are havocked and a "memory" clobber havocks reachable memory, so a
// property that depends on the asm's effect becomes unprovable rather than
// falsely proven. Here ".byte" is raw machine code CBMC does not interpret.

int g = 7;

int main(void)
{
  unsigned long out = 0;
  int *p = &g;
  __asm__ __volatile__(".byte 0x90" : "+r"(out) : "r"(p) : "memory");
  // out was an output: it is now non-deterministic, so neither of these holds
  __CPROVER_assert(out == 0, "SHOULD FAIL: out havocked");
  return 0;
}
