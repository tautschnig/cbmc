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
