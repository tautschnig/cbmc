# asm-semantics build: spin loops in lowered asm are unwound to the bound

Status: performance defect in the inline-assembly lowering
(`e09f7f3a755`, `src/assembler/asm_lowering.cpp`, REGISTER operand case,
the `w < register_width` branch). Not a soundness problem.

## Observation

`xstats_byid` (harness_xstats_byid.c, `r-as-is.gb`, `--unwind 60`):

| build | result | time | RSS |
|---|---|---|---|
| old (asm = no-op) | 1 of 7578 failed (F27) | 147 s | 4.56 GB |
| asm-semantics | no verdict after 30+ min, still in symex | – | – |
| asm-semantics + `--unwindset rte_spinlock_lock.0:2,rte_spinlock_lock.1:2` | 1 of 7604 failed (F27), both spin-loop unwinding assertions SUCCESS | 155 s | 4.57 GB |

The 26 extra properties in the last row are the dereference checks the
lowering adds for the asm memory operands.

## Mechanism

For a register operand narrower than the register, the lowering binds the
input as

    asm_opN := (asm_opN & 0xFFFFFFFF00000000) | (u64)value

to say "the upper bits are unspecified". `asm_opN` is a fresh, uninitialised
temporary, so the right-hand side is not a constant. Symex constant
propagation only propagates constants, so the value written to memory by
`xchg` (taken from `extractbits(asm_opN, 31..0)`) is a symbolic term, even
though its value is always the input.

DPDK's x86 `rte_spinlock_lock` is `xchg; test; jz 3f; 2: pause; cmpl $0,
[locked]; jnz 2b; jmp 1b`. After the first lock/unlock pair the lock word is
symbolic, so at the next `rte_spinlock_lock` symex cannot decide `jz 3f` and
unwinds both spin loops to the `--unwind` bound: about `3 * U^2` loop
iterations per call. The ENA admin queue takes `rte_spinlock_lock` for every
admin command, and the xstats harness issues several, at `--unwind 60`.

## Reproducer

`asm_spinlock_unwinds.c` / `asm_spinlock_unwinds.sh`: the DPDK spinlock asm,
lock word initialised to the constant 0, N lock/unlock pairs.

    32-bit N=1 unwind=40:      0 spin-loop unwindings, 0.02 s
    32-bit N=4 unwind=5:      72 spin-loop unwindings, 0.10 s
    32-bit N=4 unwind=10:    297 spin-loop unwindings, 0.31 s
    32-bit N=4 unwind=20:   1197 spin-loop unwindings, 1.23 s
    32-bit N=4 unwind=40:   4797 spin-loop unwindings, 4.97 s
    -DWIDE N=4 unwind=40:      0 spin-loop unwindings, 0.03 s

`-DWIDE` makes the operands 64-bit, which takes the branch that assigns the
value directly. The spin loops then disappear, which isolates the cause to
the sub-register merge.

## Possible fixes (for the asm-semantics author)

* Keep the low `w` bits and the upper bits in separate temporaries when an
  operand is narrower than the register, so the low part stays a constant
  when the input is one. Reads of the full register concatenate them.
* Or, when no template instruction reads the operand at a width wider than
  `w` (the common case: no `%q`/`%k`-style modifier), bind it at width `w`
  and skip the merge.

Zero-extending instead would be faster but would under-approximate asm that
does read the upper bits.

## Workaround in the proofs

`--unwindset rte_spinlock_lock.0:2,rte_spinlock_lock.1:2` together with
`--unwinding-assertions` is sound: if a lock could ever spin, the unwinding
assertion for that loop fails. Not yet applied to the runners.
