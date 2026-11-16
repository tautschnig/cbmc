// A sub-register (32-bit) input operand whose value is a known constant must
// stay a constant after the asm is lowered.  The lowering used to merge the
// input into an uninitialised register temporary
//   temp := (temp & 0xFFFFFFFF00000000) | value
// which is not a constant, so symex dropped it and the asm's output became
// symbolic.  For a spin lock on a known-constant word that makes the "is the
// lock free" branch undecidable, and both spin loops unwind to the --unwind
// bound.  With the fix the 32-bit input is zero-extended (temp := value) and
// stays concrete, so the spin loops resolve and a tight unwind bound with
// unwinding assertions succeeds.  See also
// integration/linux/scan/amzn-drivers/proof/cbmc-limitations/asm_spinlock_unwinds.*
typedef int word_t;
typedef struct
{
  word_t locked;
} spin_t;

static inline void spin_lock(spin_t *sl)
{
  word_t lock_val = 1;
  asm volatile(
    "1:\n"
    "xchg %[locked], %[lv]\n"
    "test %[lv], %[lv]\n"
    "jz 3f\n"
    "2:\n"
    "pause\n"
    "cmpl $0, %[locked]\n"
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

int main(void)
{
  spin_t s;
  s.locked = 0;
  for(int i = 0; i < 4; i++)
  {
    spin_lock(&s);
    spin_unlock(&s);
  }
  return 0;
}
