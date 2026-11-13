# CBMC silently assumes inline assembly does nothing

**Status: confirmed, minimally reproduced, root cause located. Not fixed.**
Written for hand-over.

**Severity: this is unsoundness, not a missing feature.** CBMC does not report
inline assembly as unsupported and does not approximate it conservatively. It
assumes the asm block had **no effect**, so a proof can hold *because of* an
effect the real code performs and the model does not. Any property whose truth
depends on state written by inline assembly is reported as proven when it may
be false.

## Minimal reproducer

`asm_set.c` in this directory, ten lines, no includes:

```c
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
```

```
$ cbmc asm_set.c
[main.assertion.1] line 10 A: CBMC thinks the asm wrote nothing: SUCCESS
[main.assertion.2] line 11 B: the asm set the bit: FAILURE
VERIFICATION FAILED
```

Assertion A **passing** is the bug: CBMC proves the variable is still zero
after an instruction that sets a bit in it.

Two more, in this directory, showing the blast radius:

| reproducer | assertion | result | meaning |
|---|---|---|---|
| `asm_memclobber.c` | `v == 7` after `asm volatile("" ::: "memory")` | SUCCESS | a `"memory"` clobber invalidates nothing |
| `asm_linuxbitop.c` | `word == 0` after Linux's `set_bit(3, &word)` | SUCCESS | the Linux kernel's bitops are affected identically |

## No diagnostic of any kind

Not from `goto-cc` at compile time, not from `cbmc` at `--verbosity 10`, and
there is no command-line option mentioning `asm`. The only way to discover it
is to write an assertion that should fail and notice that it does not.

## Root cause

`src/goto-symex/symex_other.cpp:121-124`:

```cpp
else if(statement==ID_asm)
{
  // we ignore this for now
}
```

**The information needed to do better is already present.** The front end
parses the operands and the goto program keeps them:

```
$ goto-instrument --show-goto-functions a.gb
OTHER code statement="asm" flavor="gcc"("lock; orq %1,%0",
    (gcc_asm_output("+m", main::1::flags)),
    (gcc_asm_input("ir", cast(4, unsignedbv[64]))),
    (gcc_asm_clobbered_register("cc")), )
```

So the output operands, input operands and clobber list survive into symex
(`ansi-c/goto-conversion/goto_asm.cpp` builds this). Symex simply does not
read them.

## How it was found, and why that matters

Not by looking for it. A driver-proof harness set a device-state flag with
FreeBSD's `ENA_FLAG_SET_ATOMIC`, which on amd64 expands through `ATOMIC_ASM`
(`sys/amd64/include/atomic.h:355-365`) to the `lock; orq` above. The flag was
never observed set, so a guarded path was never entered, and an obligation
about that path held for want of a path rather than because the code was
correct.

**The run looked clean**: no reached bodyless callee, no UNKNOWN, no failing
unwinding assertion, and a *different* obligation in the same harness failed,
so the verdict appeared informative. Nothing in the output hinted at the
cause. That is the practical danger: the failure mode is invisible and it
flatters the result.

## Suggested fix, in increasing order of cost

1. **Warn.** Emit a diagnostic naming the location whenever an `ID_asm`
   instruction is reached, so the gap is at least visible. Cheap, and would
   have saved the detour above.

2. **Havoc the declared outputs.** For each `gcc_asm_output`, assign a fresh
   nondeterministic value of the operand's type; for a `"memory"` clobber,
   invalidate the points-to information the analysis is relying on. This is
   sound in the direction that matters -- a property that depends on the asm's
   effect becomes unprovable rather than falsely proven -- and the operands are
   already in hand. Assertion A above would then fail, which is the correct
   outcome, and B would become unprovable rather than false.

3. **Interpret a whitelist.** A small set of patterns covers most kernel code:
   `lock; or`, `lock; and`, `lock; bts`, `lock; btr`, `lock; xadd`,
   `lock; cmpxchg`, plus the barrier-only forms. Modelling these exactly would
   let kernel atomics and bitops be verified rather than merely not
   mis-verified. This is the only option that makes properties *about* atomics
   provable.

Option 2 is the one that removes the unsoundness; option 1 is worth doing
regardless and independently; option 3 is a feature on top.

A regression test for option 2 is the reproducer above with assertion A
inverted: after the fix, `flags == 0` must **not** be provable.

## Scope for anyone verifying kernel code

Every use of the following is currently assumed to do nothing:

* FreeBSD `atomic_*` on amd64 (`ATOMIC_ASM`), and so everything built on them
  -- `BIT_SET_ATOMIC`, `BIT_CLR_ATOMIC`, driver flag words;
* Linux `set_bit`, `clear_bit`, `test_and_set_bit`, `atomic_*` on x86;
* any `barrier()` or `smp_*` built from `asm volatile ... "memory"`.

For harnesses, the workaround is to use the non-atomic equivalent in setup
code -- `BIT_SET(...)` rather than `BIT_SET_ATOMIC(...)` -- and to model the
atomics explicitly where atomicity is itself the property under test. That is
what the driver-proof harnesses now do, and it is recorded in
`doc/driver-proof-methodology-2026-11.md`.

## Files here

* `asm_set.c` -- the ten-line reproducer, FreeBSD `atomic_set_long` shape
* `asm_memclobber.c` -- a `"memory"` clobber invalidates nothing
* `asm_linuxbitop.c` -- Linux `set_bit` shape, same result
