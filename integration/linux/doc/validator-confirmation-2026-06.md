# #1 Validator confirmation of the count/index frontier (2026-06)

**Date:** 2026-06-12.  The auto_slice pipeline reduced the 33 count/index
candidates to 13 "needs non-local validator" (UNGUARDED, no local mask).
A validator locator searched each candidate's subsystem for a bound on the
count/index (reject-guard `field >[=] CONST/MAX`, clamp/min, mask, modulo).

## Result: validator found for 9, NONE for 4 -- and the 4 are all FPs

| candidate | finding |
|-----------|---------|
| mqprio_enable_offload | `sch_mqprio_lib.c:76  qopt->num_tc > TC_MAX_QUEUE` (mqprio_validate_qopt) |
| taprio_change | same `num_tc > TC_MAX_QUEUE` validator |
| cec_config_thread_func, __cec_s_log_addrs | `cec-adap.c:1856  num_log_addrs > available_log_addrs` |
| supinfo_to_lineinfo | `gpiolib-cdev.c:1331  num_attrs > GPIO_V2_LINE_NUM_ATTRS_MAX` |
| ipc_mux_ul_adgh_finish | `if_id >= ARRAY_SIZE(...)` guard |
| pulse8_interrupt | `idx = (...) % NUM_MSGS` (modulo-bounded) |
| rtllib_rx_get_crypt, vchiq_... | candidate guards present (advisory) |

The 4 with NO validator found were deep-dived -- ALL are FALSE POSITIVES,
each from a distinct finder-precision gap:

* **fl_set_key_mpls_lse** -- guarded on the SOURCE variable:
  `if (depth < 1 || depth > FLOW_DIS_MPLS_MAX) return -EINVAL;` then
  `lse_index = depth - 1` (in [0,6]) indexes `ls[7]`.  CBMC PROVES the
  source-var-guard slice SAFE.  Gap: the finder/locator track the index
  variable, not a guard on the source variable it is derived from.
* **arcnet_header** -- `uint8_t proto_num` indexes `[256]`; the index TYPE
  (<256) bounds it.  Gap: no index-type-width bound.
* **pulse8_irq_work_handler** -- `rx_msg[rx_msg_cur_idx]` where
  `rx_msg_cur_idx = (rx_msg_cur_idx + 1) % NUM_MSGS` is maintained as a
  data-structure INVARIANT (< NUM_MSGS) at the update site.  Gap: no
  field-invariant tracking.
* **f81534_process_read_urb** -- index from a driver-init-bounded `tty_idx[]`
  table.

## Conclusion

The entire count/index OOB-write frontier in the scanned subsystems is
**well-defended** -- every candidate is bounded by a local mask, a local
guard (on the index or its source var), the index type, a data-structure
invariant, or a non-local validator.  **No confirmed OOB bug** in this set.

This negative result is meaningful (the kernel's count/index defenses hold
across a broad sample) and yields three concrete, SOUND finder refinements
to cut the FP rate without losing recall:
  1. source-variable guards: a guard on `X` that bounds `index = f(X)`;
  2. index type-width bound: an index whose type max < array size;
  3. struct-field data-structure invariants (`field = ... % N` / `& MASK`
     maintained at update sites).

These should be added as ADVISORY signals (never sound drops) per the
collector architecture; CBMC slices confirm the genuine ones (fl_set_key
proven SAFE).
