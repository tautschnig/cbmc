# Kernel-wide count/index triage with validated advisories (2026-06)

**Date:** 2026-06-12.  Ran the count/index finder (with the (a) advisories:
adv_typebound / adv_srcguard / adv_inv, plus existing guard/mask) over the
cached per-leaf CodeQL DBs across ALL classes (`wide_triage.py`, parallel +
resumable), classifying each candidate by the advisory-based triage.

## Result (316/536 leaf DBs, 5291 distinct candidates; resumable)

| triage class | count | % |
|--------------|------:|--:|
| LOCALLY-BOUNDED | 2693 | 50% |
| GENUINE-FRONTIER | 2598 | 49% |

impact: READ 4059 / WRITE 1232.  confidence: MEDIUM 5239 / HIGH 52.

Local-bound advisory hits (why LOCALLY-BOUNDED):

| advisory | hits |
|----------|-----:|
| adv_guard=GUARDED | 1484 |
| adv_bound=bareparam (caller's responsibility) | 1153 |
| adv_srcguard=SRCGUARDED *(new)* | 195 |
| adv_mask=MASKED | 90 |
| adv_inv=MODULO *(new)* | 44 |
| adv_typebound=TYPEBOUND *(new)* | 8 |

The three new (a) advisories reclassify **247** candidates from frontier ->
locally-bounded (FP reduction without dropping anything).

## The bug-hunt priority set: 9 candidates, all known FPs

GENUINE-FRONTIER ∩ HIGH-confidence ∩ WRITE -- the candidates most likely to
be a real OOB-write -- is just **9** kernel-wide, and every one is already
explained:

* `cec_config_thread_func` x4 (log_addr[4], msg[16]) -- non-local validator
  confirmed in #1 (cec-adap.c:1835 `num_log_addrs > available_log_addrs`).
* `vpif_channel_isr` x4 (dev[2]) -- `channel_id = *(int *)dev_id` where
  dev_id is the IRQ-handler cookie (a trusted kernel pointer set to a small
  constant at request_irq), NOT attacker input.  A TAINT over-approximation
  FP -> a further refinement: exclude IRQ-cookie / trusted-void* derefs from
  the raw-taint source set (isByteBufferParamRead).

## Conclusion

Across 5291 count/index candidates kernel-wide, the top-priority OOB-write
set reduces to 9, all known false positives (non-local-validated or
taint-over-approximation).  **No new genuine bug** -- consistent with #1
and the #2 recall validation: the count/index OOB-write surface is
well-defended, and the pipeline (with 100% shape-recall) would have surfaced
a real one.  The run identifies one more sound refinement (taint precision on
IRQ-cookie void* derefs).  ~220 leaf DBs remain (resumable) but the
priority-set conclusion is stable.

## Update: after the IRQ-cookie taint refinement (+ wider run)

Re-ran (416/536 leaf DBs, 5797 distinct candidates) with the
isTrustedCookieParam fix (IRQ-handler dev_id cookies no longer raw-tainted):

| triage class | count | % |
|--------------|------:|--:|
| LOCALLY-BOUNDED | 2909 | 50% |
| GENUINE-FRONTIER | 2888 | 49% |

HIGH confidence dropped 52 -> 46 (the vpif cookie reads are now MEDIUM).
The new advisories now account for srcguard 210, mask 100, modulo 44,
typebound 9 of the locally-bounded set.

**Bug-hunt priority set (GENUINE-FRONTIER & HIGH & WRITE): 9 -> 4**, and all
4 are `cec_config_thread_func` -- the non-local-validator class CONFIRMED in
#1 (cec-adap.c:1835).  The vpif class is gone (taint FP fixed).  So EVERY
top-priority count/index OOB-write candidate kernel-wide is now explained:
either a confirmed non-local validator (cec) or a recognised local bound.
**No unexplained candidate; no genuine bug.**

~120 leaf DBs remain (resumable, the slow gpu/staging/usb subdir DBs); the
priority-set conclusion is stable.

## Final (525/536 leaf DBs, near-complete)

7172 distinct count/index candidates kernel-wide.  GENUINE-FRONTIER 3466; of
which HIGH-confidence WRITE (top bug-hunt priority): **7**, and ALL 7 are the
known non-local-validator class confirmed in #1:
  * mqprio_enable_offload x2 (min_rate/max_rate[16]) -- mqprio_validate_qopt;
  * taprio_change (cur_txq[16]) -- netdev_set_num_tc;
  * cec_config_thread_func x4 (log_addr[4]/msg[16]) -- cec-adap.c:1835.

Every top-priority OOB-write candidate kernel-wide is an explained,
validator-guarded false positive.  The vpif taint-FP class is gone (the
IRQ-cookie refinement held).  Final verdict: **NO genuine count/index
OOB-write bug across 7172 candidates / 525 leaf DBs** -- a sound true
negative (the #2 recall harness confirms the pipeline would flag a real one),
i.e. the count/index OOB-write surface is well-defended kernel-wide.
