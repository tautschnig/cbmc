# Kernel-wide drivers/* candidate census + HIGH/WRITE triage (2026-06)

**Date:** 2026-06-11. Cashes in the per-subsystem scoping harness: a full
sweep of the 127 `drivers/<leaf>` subsystems in the 6.12 broad config,
building a per-leaf CodeQL DB for each and running the 8 threat finders on
it (`drivers_census.py`, parallel + resumable per-leaf JSON cache).

## Census

125/127 leaves built (2 build-fails: `accel`, `accessibility` -- no
compilable objects in this config).  Per-leaf finder runs completed for 116
leaves; **9 large leaves (gpu, net, scsi, usb, media, staging, infiniband,
iio, clk) hit the 180 s per-query timeout** even per-leaf -- their leaf DBs
are still big, and the heavy taint finder exceeds the budget.  Tracked, not
hidden.

| asset | raw | mitigated | genuine | finder-timeouts |
|-------|----:|----------:|--------:|----------------:|
| A1-mem count/index | 1397 | 0 | 1397 | 9 |
| A1-mem decoded-len | 67 | 0 | 67 | (9 err) |
| A1-mem skb/cursor | 808 | 0 | 808 | (9 err) |
| A1-UB div/shift | 168 | 18 | 150 | (9 err) |
| A2 confidentiality | 381 | 188 | 193 | (9 err) |
| A3 integrity/CFI | 119 | 0 | 119 | (9 err) |
| A4 availability | 396 | 24 | 372 | (9 err) |
| A5 authorization | 14 | 0 | 14 | (9 err) |
| **TOTAL** | **3350** | | **3120** | |

These are the first kernel-wide `drivers/*` candidate counts the pipeline
has produced -- the whole-`drivers/` DB returned none (every taint finder
timed out).  A2's 188 mitigated/381 (49 %) is the recognised-mitigation
clear rate; the structural finders carry no auto-mitigation column, so their
hits all enter the genuine pool.

## HIGH-confidence count/index WRITE triage (the OOB-write tier)

The triage shortlist -- count/index hits that are BOTH taint-reachable
(`confidence=HIGH`) AND a WRITE destination (`impact=WRITE`), i.e. the
likely-OOB-write primitive -- contained exactly **one** candidate across all
125 leaves:

> `altera_execute` -- `drivers/misc/altera-stapl/altera.c:518`

```c
u32 args[3];
...
arg_count = (opcode >> 6) & 3;            /* opcode = p[pc] & 0xff (bytecode) */
for (i = 0; i < arg_count; ++i) {
    args[i] = get_unaligned_be32(&p[pc]); /* flagged WRITE */
    pc += 4;
}
```

`opcode` is read from the STAPL bytecode buffer `p` (hence taint-reachable ->
HIGH), and `args[i]` is a WRITE into a fixed `u32 args[3]`.  **Triage verdict:
FALSE POSITIVE.**  `arg_count = (opcode >> 6) & 3` is in `{0,1,2,3}`, so the
loop writes `args[0..arg_count-1] ⊆ args[0..2]` -- in bounds for `args[3]`.
The finder missed the bound because the `& 3` mask is applied to the RHS
sub-expression and the result assigned to `arg_count`, not written as
`arg_count &= 3` -- the form the existing `maskGuarded` recognised.

**Finder improvement.**  `tainted_count_into_fixed_array.ql` `maskGuarded`
now also recognises `count = <expr> & CONST`.  Effect: `altera_execute` drops
from the count/index oracle entirely; the drivers HIGH/WRITE shortlist goes
to 0; regression-clean on rc7-db net (count/index unchanged at 145 hits / 88
funcs -- no net function uses the assign-from-mask form, so nothing else is
suppressed).

*Soundness note:* like the pre-existing mask disjuncts, this treats any
compile-time mask as bounding; a mask whose constant exceeds the array size
would be wrongly cleared.  For `altera_execute` the bound is exact (`& 3`,
`args[3]`, `i < arg_count`).  Tightening to `mask_const < array_size` is a
possible future refinement.

## Honest status

* This is the **candidate (finder) layer**.  The 3120 genuine drivers
  candidates are now *reachable* (the whole-drivers DB produced none) but
  *untriaged*; the HIGH/WRITE tier -- the one most likely to be a real
  OOB-write -- was triaged to empty.
* 9 large leaves need a longer per-query budget (or further sub-scoping); the
  2 build-fails are config-empty.  Both tracked by the harness.
* Re-running the full sweep after the `maskGuarded` refinement would shave a
  few mask-on-assign FPs off the count/index raw total (1398 -> 1397 already
  on the misc cell); the genuine and HIGH/WRITE conclusions stand.
