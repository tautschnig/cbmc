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
* `asm_dpdk_atomic.c` -- DPDK's `rte_atomic32_inc` under this build's flags
  (needs the DPDK include path)
* `asm_dpdk_atomic.c` -- DPDK's `rte_atomic32_inc` under this build's flags,
  showing the increment is unobserved (needs the DPDK include path)

## Audit of this proof corpus against the defect (2026-11-13)

Done after the defect was found, because a silently-ignored write can make an
obligation hold for want of a path. **No existing obligation is invalidated.**
The reasoning, per mechanism, so it can be re-checked:

**DPDK's atomics are affected too**, not only FreeBSD's. `rte_atomic32_inc`
under this build's flags is inline assembly (`RTE_FORCE_INTRINSICS` is not
set), and the ten-line test shows the increment is unobserved:

```
E: DPDK atomic inc does nothing: SUCCESS
F: DPDK atomic inc is observed: FAILURE
```

So `ATOMIC32_*` (which `ena_plat_dpdk.h:270-273` maps to `rte_atomic32_*`) and
`rte_atomic64_*` are all no-ops to the analysis.

**Where atomic-written state exists in the driver.** Two counters, and nothing
else:

| state | written by | read by |
|---|---|---|
| `admin_queue->outstanding_cmds` | `ATOMIC32_INC/DEC/SET` (`ena_com.c:189, 215, 2098`) | the queue-full guard (`:238`) and the abort wait loop (`:1509`) |
| `drv_stats->ierrors / oerrors / rx_nombuf` | `rte_atomic64_inc` (`ena_ethdev.c:1796, 2855`) | `ena_stats_get` (`:1255-1257`) |

**Why no obligation is affected.**

1. *No DPDK harness sets a guard through an atomic.* The FreeBSD
   `stats_sample_interval` harness did, which is how the defect surfaced; it
   is fixed and no other harness does it. Checked by grep across all 46
   harnesses: only `harness_reset_flag.c` and `fbsd/harness_fbsd_sysctl.c`
   mention atomics at all, and `reset_flag` uses plain assignment.

2. *The two guard sites fail safe, not silently.* With `outstanding_cmds`
   stuck at zero, the queue-full guard at `:238` never refuses, so the
   harnesses explore **more** submissions than the real driver would accept --
   which widens the state space for a safety property rather than narrowing
   it. The abort wait at `:1509` exits immediately, and the harness that
   reaches it (`harness_leaks.c`) models
   `ena_com_wait_for_abort_completion` as a no-op anyway, so nothing changes.

3. *No obligation asserts on the counters.* `harness_xstats.c` asserts
   `XS_COUNT`, the number of statistics reported, not their values.

4. *The witnesses confirm it empirically.* Every admin-path harness still has
   its witnesses failing -- `conc_admin`'s "the submit can complete" and "the
   device can complete the new command" both FAILURE in the last sweep -- so
   the submission and completion states really do arise. That is the check
   that would have caught a lost path, and it passes.

**The direction to watch.** The dangerous case is an atomic write that a guard
later tests, because the guard then never fires and a whole branch disappears
with no diagnostic. That is exactly what happened to the FreeBSD flag. Any new
harness that sets state the driver branches on must use the non-atomic form,
and a witness on *both* directions of that branch is the thing that catches it.

## Resolution (follow-up implementation)

Fixed on branch `asm-semantics`. Inline assembly is no longer assumed to do
nothing. Every statement is now either interpreted exactly or, if some
instruction in it is not understood, approximated conservatively (its outputs
and, for a `"memory"` clobber, the reachable memory are havocked) with a
warning. The silent third outcome -- delete the statement, assume no effect --
is gone. See `doc/architectural/inline-assembly.md` for the design.

On the three reproducers here, with the fixed build:

* `asm_set.c`: `flags == 4` holds and the old `flags == 0` now **fails**, as it
  should -- the write is observed.
* `asm_linuxbitop.c`: `word == 0` after `set_bit(3, &word)` now **fails**.
* `asm_memclobber.c`: `v == 7` still holds, which is correct -- the barrier
  `asm volatile("" ::: "memory")` executes no instruction, so it has no effect
  on the C state (a `"memory"` clobber constrains what the *instructions* may
  touch; it does not add effects to a statement with none).

x86, AArch64 and AArch32 instruction semantics are implemented, covering the
data-processing, bit, shift, atomic (`lock`-prefixed, load/store-exclusive,
LSE), flag and branch instructions used by kernel and driver code; the
barriers of all architectures are recognised regardless of the configured one.
The real FreeBSD `machine/atomic.h` atomics (`atomic_set_long`,
`atomic_clear_long`, `atomic_add_long`, `atomic_fetchadd_int`, ...) and the
Linux `set_bit` shape verify exactly. The semantics are checked against
hardware and the Unicorn emulator by `scripts/asm_differential/`, which
generates the `regression/cbmc/asm_{x86,aarch64,arm}` tests.

The harness workaround described above (use the non-atomic form in setup
code) is therefore no longer necessary for the supported instructions, though
it remains harmless.

### Re-verification of this corpus under the fixed semantics (2026-11-15)

The audit above concluded that no obligation was invalidated, partly because
the two guards that read atomic state "fail safe": with `outstanding_cmds`
stuck at zero the queue-full guard never refused, which *widened* the
explored space. Under the fix those guards are live, which **narrows** it --
the direction that can hide a path -- so the affected harnesses were re-run
with the fixed build rather than assumed still sound:

* `asm_dpdk_atomic.c` now reports "does nothing" FAILURE and "is observed"
  SUCCESS, confirming `rte_atomic32_inc` is modelled;
* `harness_admin_cq.c`: `AC_HEAD`, `AC_PHASE`, `AC_HEADS_AGREE` and
  `AC_UNOCCUPIED` all still SUCCESS, three witnesses still FAILING, no other
  failing property -- unchanged;
* `harness_conc_admin.c`: `CC2_NO_STALE_CQE` still fails in the configuration
  that reproduces the finding and holds in the other, witnesses still
  FAILING -- unchanged.

So the conclusion stands, now for a better reason: it holds with the atomics
modelled rather than because they were invisible. The whole corpus is being
re-swept under the fixed build.

A contract mechanism for inline assembly -- a way to state the effect of a
statement CBMC cannot interpret, so a proof can still go through -- is designed
in `doc/architectural/inline-assembly.md` but not yet implemented.
