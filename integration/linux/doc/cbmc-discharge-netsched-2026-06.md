# CBMC discharge: net/sched HIGH/WRITE candidates (2026-06)

**Date:** 2026-06-11.  The two fresh genuine-ranked OOB-write candidates the
all-class census surfaced in net/sched (tc qdisc offload).  Advisories rank;
CBMC is the definitive filter.

## Front-end: clean (thanks to the __seg_gs fix)

`goto-cc net/sched/sch_mqprio.c` and `sch_taprio.c` now build cleanly with no
-D workaround -- the `__seg_fs`/`__seg_gs` scanner fix already covers these
x86 TUs (only benign list.h pointer-conversion warnings).  No new front-end
gap.

## CBMC scaling limitation encountered

Whole-function `cbmc --function mqprio_enable_offload --bounds-check` stalls
in BMC: function-pointer removal of the `dev->netdev_ops->ndo_setup_tc(...)`
indirect call pulls in every `ndo_setup_tc` implementation in the TU, and the
full body (mqprio_fp_to_offload, etc.) explodes the formula.  This is the
documented large/complex-function case -> discharge the specific OOB-write
property with a verbatim SLICE (as for cec/altera).  A future improvement is
fn-ptr stubbing (goto-instrument / --function-pointer-restrictions) to make a
whole-body proof tractable; not required for the property here.

## Verdicts (verbatim slice + non-local precondition)

`mqprio_enable_offload` -- `real_mqprio_offload.c`: min_rate/max_rate copy
loops `for(i<qopt.num_tc)` into `[TC_QOPT_MAX_QUEUE=16]`, num_tc a __u8.

```
# verbatim (num_tc nondet, up to 255):
[main.array_bounds] 'mqprio'.min_rate / .max_rate upper bound ...: FAILED
VERIFICATION FAILED
# with mqprio_validate_qopt precondition (num_tc <= TC_QOPT_MAX_QUEUE):
VERIFICATION SUCCESSFUL
```

`taprio_change` -- `real_taprio_curtxq.c`: `q->cur_txq[i] = mqprio->offset[i]`
for `i<mqprio->num_tc`, `cur_txq[TC_MAX_QUEUE=16]`, `offset[16]`.

```
# verbatim:
[main.array_bounds] 'm'.offset upper bound ...: FAILURE
VERIFICATION FAILED
# with netdev_set_num_tc precondition (num_tc <= TC_MAX_QUEUE):
VERIFICATION SUCCESSFUL
```

## Conclusion

Both are REAL OOB-write shapes (CBMC witnesses the violation on the verbatim
body -> correctly NOT cleared, validating the sound-collector keep), safe
ONLY under their non-local validators (mqprio_validate_qopt at sch_mqprio.c
:134; netdev_set_num_tc at sch_taprio.c:1898).  The correct discharge is a
caller-precondition proof carrying `num_tc <= 16`.  Same conclusion class as
the CEC cluster: the sound collector keeps them, the advisory framework-
validation does not drop them, and CBMC + the documented precondition decide.
