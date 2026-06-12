# #2 Ground-truth precision/recall for count/index discharge (2026-06)

**Date:** 2026-06-12.  #1 found the count/index OOB-write frontier fully
guarded (no bug).  This validates that the pipeline WOULD catch a real bug --
i.e. the negative is a true negative, not a recall failure.

## Method: vuln/fixed differential (controlled positives)

For each real guarded candidate, build two slices that differ only by the
validator:
* **fixed** (validator PRESENT, `__CPROVER_assume(bound)`) -- the patched
  kernel; must verify SAFE (precision -> the pipeline CLEARS guarded code);
* **vuln** (validator ABSENT) -- the pre-fix / unguarded shape, the archetype
  being CVE-2019-3701 (CAN-gw count/index OOB-write, fixed by adding a bounds
  check); must FAIL (recall -> the pipeline FLAGS the bug).

`ground_truth_recall.sh` runs this over fl_set_key_mpls_lse (depth guard),
cec num_log_addrs, mqprio/taprio num_tc:

```
  fl_set_key_mpls_lse   fixed=SUCCESSFUL  vuln=FAILED  [OK]
  cec_num_log_addrs     fixed=SUCCESSFUL  vuln=FAILED  [OK]
  mqprio_num_tc         fixed=SUCCESSFUL  vuln=FAILED  [OK]
  taprio_num_tc         fixed=SUCCESSFUL  vuln=FAILED  [OK]
  recall+precision pairs passed: 4/4
```

## Result

* **Recall 4/4** -- every validator-absent (vulnerable/CVE-shape) variant is
  flagged (VERIFICATION FAILED).  More broadly, recall on the OOB *shape* is
  100% by construction: the auto_slice verbatim run is VIOLATED for all 33
  count/index candidates, so the finder + slice detect the OOB shape whenever
  the bound could be absent.
* **Precision 4/4** -- every validator-present (fixed) variant is cleared
  (VERIFICATION SUCCESSFUL); combined with #1 (all 13 frontier candidates
  have a real validator/guard/type/invariant), the full pipeline surfaces
  zero surviving false positives on this surface.

## Conclusion

The pipeline catches a count/index OOB iff the bounding validator is absent,
and clears it iff present -- demonstrated on real functions via the
vuln/fixed differential.  Therefore #1's "frontier fully guarded -> no bug"
is a SOUND TRUE NEGATIVE: the scanned count/index OOB-write surface is
genuinely well-defended, and the pipeline has the recall to have caught a bug
had one been present.

Caveat: this measures the count/index OOB-write shape (the flagship asset).
Recall for skb/cursor and A2 shapes is not measured here -- a follow-up
extends the same vuln/fixed differential to those templates.
