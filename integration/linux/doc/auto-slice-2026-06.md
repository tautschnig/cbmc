# Auto-slicer: definitive count/index discharge at scale (2026-06)

**Date:** 2026-06-12.  The discharge survey showed the verbatim SLICE is the
only approach that scales to definitive CBMC verdicts (raw `--function` =
nondet-input artifacts; full-function `goto-harness` = ~85% timeout).
`auto_slice.py` auto-generates that slice from a count/index finder hit -- no
source parsing beyond the array size N, the count/index, and the kind:

```
count-loop-write:  unsigned char arr[N]; __CPROVER_assume(i<count); arr[i]=0;
direct-index:      unsigned char arr[N]; arr[idx]=0;
```

The count/index is left fully NONDET (a sound over-approximation; the real
value can only be more constrained), and the loop body is modelled by one
arbitrary in-range iteration (sound, N-independent, no unwinding).  Each
slice is checked twice:

* verbatim (count unbounded)        -> **VIOLATED**  (the OOB is reachable)
* with `__CPROVER_assume(count<=N)` -> **PROVED**     (N is a sufficient bound)

## Result: 33/33 count/index candidates -> "OOB-shape; SAFE iff count<=N"

Every candidate gives the (VIOLATED, PROVED) pair, instantly.  This is the
definitive characterization: each is a real index-OOB shape whose safety is
EXACTLY the single bound `count/index <= N`.  Triage is thereby reduced to
one targeted question -- "is the count/index bounded by N?" -- answered by
the (advisory) local-bound signal:

| | count | examples |
|--|------:|----------|
| **locally bounded** (adv_mask=MASKED or adv_guard=GUARDED) -> bound established locally -> SAFE | 20 | altera (arg_count=(opcode>>6)&3<=3), acpi_ut_validate_resource ((rtype&MASK)>>3<=15), nfc_hci_cmd_received (pipe<128 guard), hiddev_*, afs_*, __cec_s_log_addrs, megasas_*, rename_volumes, ax25_rt_add |
| **needs a non-local validator** (UNGUARDED, no mask) -> bound is a non-local obligation -> GENUINE TARGET | 13 | mqprio_enable_offload, taprio_change, cec_config_thread_func, pulse8_interrupt/irq_work_handler, fl_set_key_mpls_lse, arcnet_header, vchiq_compat_ioctl_queue_message, rtllib_rx_get_crypt, ipc_mux_ul_adgh_finish, supinfo_to_lineinfo, f81534(16) |

## Soundness layering (consistent with the collector architecture)

* The auto-slice verdict is **sound**: it PROVES `count<=N` is necessary
  (verbatim VIOLATED) and sufficient (precond PROVED) for safety.
* The locally-bounded/needs-validator split uses the **advisory** signal
  (unsound heuristic) -- so the 20 are "advisory-locally-safe, pending
  confirmation that the local guard/mask actually yields `<=N`".  Definitively
  clearing them needs CBMC on the count's REAL derivation (as the manual
  altera slice did: it proved `(opcode>>6)&3 <= 3`); the nondet-count
  auto-slice intentionally discards that derivation.
* The 13 needs-validator candidates are the genuine frontier: real OOB shapes
  whose only safeguard is a non-local `count<=N` validator (mqprio_validate_
  qopt, netdev_set_num_tc, the CEC/cur_idx invariants, ...).

## The discharge pipeline, end to end

1. **collector** (sound, over-approximate) -> candidates;
2. **auto_slice** (CBMC, sound) -> "safe iff count<=N" for each;
3. **advisory** (ranking) -> locally bounded vs needs-non-local-validator;
4. (next) inline the count's real derivation into the slice to definitively
   clear the locally-bounded set, and a validator search / caller-precond
   slice to settle the 13.

## Update: derivation inlining (--derive) -- definitive local clears

`auto_slice.py --derive` upgrades the advisory "locally bounded" set to
DEFINITIVE proofs where possible: it extracts the count/index variable's
single-line assignment RHS from source, resolves ALL-CAPS macros to integer
literals (grepped from the tree, e.g. ACPI_RESOURCE_NAME_SMALL_MASK=0x78),
strips type casts, declares the free inputs nondet, inlines the REAL
expression, and proves the array access safe with NO precondition.  Sound:
it claims PROVED-SAFE only when CBMC proves it; otherwise it falls back to
the advisory (never overclaims).

Result over the 5 MASKED candidates:

| candidate | derivation | verdict |
|-----------|-----------|---------|
| altera_execute | `arg_count = (opcode >> 6) & 3` | **PROVED-SAFE** (no precond) |
| acpi_ut_validate_resource | multiple assignments; extractor took `resource_type - 0x70` (large-resource path, bound set by an earlier range guard) | VIOLATED -> not locally proven (stays advisory) |
| hiddev_lookup_report, csio_mb_fwevt_handler, longest_match_std | RHS is a call/array/member access | RHS-too-complex -> advisory |

So the simple single-line mask case (altera) is now a definitive,
heuristic-free clear; multi-assignment / call-derived counts need backward
slicing of the real derivation (goto-instrument --full-slice w.r.t. the
bounds property, or multi-line tracing) -- the next refinement.

## Pipeline status (count/index)

1. collector (sound) -> 33 candidates;
2. auto_slice (sound) -> all 33 "safe iff count<=N";
3. advisory -> 20 locally bounded / 13 non-local-validator targets;
4. --derive (sound) -> definitively clears the simple-mask subset
   (altera proven SAFE); complex derivations fall back to advisory.

The non-local-validator 13 remain the genuine frontier (settle via a
caller-precondition slice carrying the validator's `count<=N`, several
validators already identified: mqprio_validate_qopt, netdev_set_num_tc).

## Update: goto-slicing (negative) + sound derivation + validator search

**Effort A -- goto-level slicing: NEGATIVE.**  Tried `goto-instrument
--bounds-check` then `--full-slice` then cbmc on altera and acpi.  Full-slice
removes almost nothing (every array/pointer op generates a bounds assert, so
nearly all code feeds *some* assertion: acpi 98 KB vs 107 KB), and both still
time out.  Slicing w.r.t. ONE specific property needs the property id a
priori.  Conclusion: goto-slicing does not tame these; source-derivation
handles the simple cases, the rest stay advisory.

**Effort B -- validator search + sound derivation.**  A crude grep for a
bound on the count/index over-matched (use-sites look like bounds), but it
surfaced that the advisory's "locally bounded" detection MISSES two operator
classes: modulo (`idx = (...) % NUM_MSGS`, pulse8_interrupt) and byte-shift
(`idx = skb->data[..] >> 6`, rtllib_rx_get_crypt).

Extending `--derive` to these exposed -- and the fix enforces -- two
SOUNDNESS requirements (both were initially violated and would have produced
FALSE "PROVED-SAFE"):

1. **All reaching definitions must prove.**  Matching only the first `var =`
   often grabs a benign initializer (`int idx = 0;`) and misses the dangerous
   real assignment.  Now every textual `var =` def must prove safe.
2. **Local scalars only.**  A struct FIELD (`num_log_addrs`, ...) can be set
   via struct-copy / memcpy that `field =` regex cannot see, so "all defs
   found" is false -- cec was briefly falsely PROVED this way.  `--derive`
   now restricts to `adv_bound=local` vars that are neither address-taken nor
   cremented / compound-assigned, where textual `=` defs ARE complete.

Sound result: only `altera_execute` (local `arg_count=(opcode>>6)&3`) is
DEFINITIVELY cleared; everything else declines to advisory (18 skipped as
non-local storage, 7 not-locally-proven, 7 RHS-too-complex) -- ZERO false
clears.  The byte-shift (rtllib) and field-invariant (pulse8/cec) cases need
type-aware abstraction or a non-local-validator proof respectively.

## Net status

* count/index discharge: collector -> auto_slice (sound: 33 "safe iff
  count<=N") -> advisory split -> `--derive` (sound: definitively clears
  local-scalar simple-mask; altera).
* The genuine non-local-validator frontier (mqprio/taprio/cec, validated;
  plus the field-invariant pulse8/rtllib) is settled by a caller-precondition
  slice carrying `count<=N` -- already PROVED-with-precond in auto_slice; the
  remaining step is confirming the validator establishes that bound (done
  manually for mqprio/taprio/cec).
* Two soundness pitfalls in source-derived slicing were found and closed
  (initializer-only match; struct-field reaching-defs).
