// The x86 rte_spinlock_lock/unlock asm from DPDK, called N times on a lock
// whose value is a known constant.  Under the asm-semantics build the
// register temporary for each 32-bit input operand is assigned
// (old_temp & 0xFFFFFFFF00000000) | value; old_temp is uninitialised, so the
// result is not a constant and symex constant propagation drops it.  The lock
// word written by xchg is therefore symbolic, and from the second call on
// symex cannot decide "jz 3f" and unwinds both spin loops to the --unwind
// bound.  See asm_spinlock_unwinds.md and asm_spinlock_unwinds.sh.
//
// -DWIDE makes the operands 64-bit (register-width), which takes the branch
// of the lowering that assigns the value directly: the spin loops are then
// never unwound.  That isolates the cause to the sub-register merge.
#ifdef WIDE
typedef long word_t;
#  define CMP "cmpq"
#else
typedef int word_t;
#  define CMP "cmpl"
#endif
typedef struct { word_t locked; } spin_t;
static inline void spin_lock(spin_t *sl)
{
  word_t lock_val = 1;
  asm volatile("1:\n"
               "xchg %[locked], %[lv]\n"
               "test %[lv], %[lv]\n"
               "jz 3f\n"
               "2:\n"
               "pause\n"
               CMP " $0, %[locked]\n"
               "jnz 2b\n"
               "jmp 1b\n"
               "3:\n"
               : [locked] "=m"(sl->locked), [lv] "=q"(lock_val)
               : "[lv]"(lock_val)
               : "memory");
}
static inline void spin_unlock(spin_t *sl)
{
  word_t v = 0;
  asm volatile("xchg %[locked], %[ulv]\n"
               : [locked] "=m"(sl->locked), [ulv] "=q"(v)
               : "[ulv]"(v)
               : "memory");
}
int nondet_int(void);
int main(void)
{
  spin_t s;
  s.locked = nondet_int();
#ifdef KNOWN
  s.locked = 0;
#endif
  for(int i = 0; i < N; i++)
  {
    spin_lock(&s);
    spin_unlock(&s);
  }
  return 0;
}
