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


## Update (2026-06-11): census gap closed + next-tier triage

Re-ran the 9 timed-out large leaves with a 600 s per-query budget and the
refined finder (`DCENSUS_Q_TIMEOUT=600`): **all 9 now complete 8/8 finders,
0 timeouts** -- the census now covers all 127 leaves.  Complete totals
(the 9 big leaves -- gpu/net/staging dominate -- add ~7100 candidates):

| asset | raw | genuine |
|-------|----:|--------:|
| A1-mem count/index | 6101 | 6101 |
| A1-mem skb/cursor | 2360 | 2360 |
| A4 availability | 765 | 716 |
| A2 confidentiality | 571 | 300 (271 mit) |
| A1-UB div/shift | 338 | 311 |
| decoded-len | 132 | 132 |
| A3 CFI | 131 | 131 |
| A5 authorization | 60 | 60 |
| **TOTAL** | **10458** | **10111** |

### Next-tier triage (HIGH count/index READ+WRITE + skb/cursor) -> 3 FP classes

Triaging the HIGH-confidence count/index candidates surfaced three recurring
false-positive classes:

1. **Mask-through-shift/cast** -- a count/index assigned from a value masked
   with a compile-time constant, nested under a right-shift or cast so the
   simple `count &= CONST` form is missed.  Canonical: ACPI
   `resource_index = (u8)((rtype & ACPI_RESOURCE_NAME_SMALL_MASK) >> 3)`
   (range 0-15 << `acpi_gbl_resource_types[36]`).  **REFINED, sound:**
   `maskGuarded` now recognises `count = <maskBoundedExpr>` where
   `maskBoundedExpr` is a bitwise-AND-with-constant possibly wrapped in
   right-shifts (value only decreases) / casts.  Effect: ACPI
   `acpi_ut_validate_resource` drops (3 -> 0); altera stays cleared;
   regression-clean (rc7-db net count/index unchanged at 145 hits / 88
   funcs -- the byte-shift case `idx = skb->data[..] >> 6` is deliberately
   NOT suppressed, since `>> 6` only bounds an 8-bit operand).

2. **Non-local framework-validation** -- the dominant HIGH/WRITE class (the
   20 media hits): a scalar count FIELD validated by a `field > BOUND ->
   return -EINVAL` reject-guard in the subsystem core/ioctl path, then
   consumed by a worker thread / driver callback with no local guard.
   Confirmed FP: `cec_config_thread_func` loops `for (i < num_log_addrs)
   log_addr[i]` with `log_addr[CEC_MAX_LOG_ADDRS=4]`, and
   `num_log_addrs` is validated `> available_log_addrs (<=4)` at
   cec-adap.c:1835.  Same shape as V4L2 `num_planes <= VIDEO_MAX_PLANES`.
   This is the scalar analogue of the validate-at-storage caller-precondition
   shape; mechanising it (a non-local scalar reject-guard recogniser that
   demotes such candidates) is the recommended next refinement -- left
   unimplemented here to avoid an unsound blanket suppressor without careful
   dominance design.

3. **Trusted-`void*` over-taint** -- `vpif_channel_isr`
   `channel_id = *(int *)(dev_id)` flagged HIGH because the byte-buffer-param
   taint heuristic treats a `void *` deref as raw wire data; but `dev_id` is
   the IRQ-handler cookie (a trusted kernel pointer set to a small constant
   at `request_irq`), not attacker input.  A taint-precision refinement
   (exclude IRQ-cookie / container_of bases from `isByteBufferParamRead`).

The skb/cursor genuine set (2360) is the volume tier; spot samples are
bounded-cursor reads whose guards are the same non-local / producer families
-- i.e. classes (2) and the validate-at-storage shape already cover the
mechanisable part; the rest need per-driver or framework-contract reasoning.

*Census-number caveat:* count/index 6101 is computed with the refined finder
on the 9 re-evaluated large leaves and the pre-refinement finder on the 116
small leaves; the refinement removes only a handful on the small leaves
(ACPI 3, altera 1), so the totals are accurate to within ~single digits.
