# Whole-kernel destructor-completeness bug-hunt — 2026-06

This documents the first whole-kernel application of the
destructor-completeness detector
(`destructor_completeness.py`) as a bug-hunting tool, via the
batch driver `destructor_scan.py`.  The goal was to find
*undisclosed* incomplete-destructor memory leaks (the bug
class behind CVE-2023-53453 and CVE-2023-53697).

## TL;DR

* The detector was scaled to scan every destructor-shaped
  function across all four LTS trees.
* Iterative triage drove the per-tree candidate count down
  from **667 → 88** (linux_5_10) by eliminating six classes
  of false positive, each fixed in the detector.
* The two known CVEs remain detected throughout (the scan is
  a strict superset of the validated detection).
* Deep hand-triage of the surviving top candidates found
  **no confidently-confirmed new bug from static analysis
  alone**, but surfaced **one plausible real low-severity
  leak** — `nx842_pseries_exit` leaking `->counters` on the
  module-unload path — that persists from 5.10 to mainline
  and is worth maintainer / kmemleak confirmation.
* The exercise precisely characterised the false-positive
  mechanisms that any whole-kernel destructor analysis must
  handle; several are now filtered, others are documented as
  fundamental limitations of static (non-points-to) analysis.

## Method

`destructor_scan.py` walks a tree, and for each function
that (a) bare-`kfree`s a struct object and (b) has a
destructor-shaped name (`*_fini`/`*_free`/`*_destroy`/
`*_release`/`unregister_*`/…) and (c) is not
constructor-shaped, runs the completeness analysis:

* the **owned-field set** of the struct type is computed from
  cross-function evidence — fields freed by a *redundant
  sibling destructor* (one function that frees both the
  struct and its fields) or allocated by the *name-paired
  constructor*;
* the destructor's own direct + transitive (helper-delegated)
  frees are subtracted;
* a non-empty remainder is a candidate leak.

Per-directory indices and a global cross-directory
helper-free index make the scan tractable (minutes per tree).

## Candidate-count trajectory (linux_5_10)

| Stage | Candidates | FP class eliminated |
|---|---:|---|
| name filter = "not constructor" | 667 | (neutral-named error-path frees) |
| + require destructor-shaped name | 168 | neutral-named transient-buffer frees |
| + global cross-dir helper-free index | 163 | helper delegation (part_release → hd_free_part) |
| + param-type only when freed obj is the param | ~150 | void*-arg misattribution (ldc_free_exp_dring) |
| + recognise `vfree` and local-alias frees | 111 | mm_iommu_do_free (vfree), maple (alias) |
| + struct-name-collision guard | **88** | btrfs `workspace`, hotplug `slot` |

Final per-tree totals (v5): 88 / 85 / 80 / 79 for
linux_5_10 / 6_1 / 6_6 / 6_12.  49 candidates appear in all
four trees (potential still-unfixed); 48 of those have a
non-generic missing field.

## False-positive mechanisms (characterised)

Fixed in the detector:

1. **Cross-directory helper delegation** — a destructor
   delegates field teardown to a helper in another directory
   (e.g. `part_release` → `hd_free_part` in `block/blk.h`
   frees `part->info`).  Fixed with a whole-tree global
   helper-free index (.c and .h).
2. **void\*-argument misattribution** — a destructor frees a
   non-struct argument; resolution fell back to the first
   struct parameter (e.g. `ldc_free_exp_dring` frees `buf`,
   inheriting `struct ldc_channel`'s `mssbuf`).  Fixed by
   inheriting a parameter's type only when the freed object
   *is* that parameter.
3. **`vfree` not recognised** — the free-API regex was
   `k`-anchored, missing bare `vfree` (e.g. `mm_iommu_do_free`
   does `vfree(mem->hpas)`).  Fixed.
4. **Local-alias frees** — `local = obj->field; kfree(local)`
   (e.g. `maple_release_device` does `mq = mdev->mq;
   kfree(mq)`).  Fixed with one-level alias resolution.
5. **Struct-name collisions** — the same type name with
   different definitions in one directory (`struct workspace`
   in btrfs zlib/lzo/zstd; `struct slot` across PCI hotplug
   drivers) conflates the field evidence.  Fixed by skipping
   types with >1 definition in the directory.

Fundamental limitations (not fixed — require points-to /
indirect-call analysis):

6. **Indirect / callback frees** — the field is freed in a
   registered `.release`/`.destroy` op invoked by subsystem
   core code through a function pointer, which a static call
   graph cannot follow.  Examples:
   * `rapl_remove_package` — `rp->domains` is freed in
     `release_zone`, the powercap zone `.release` callback
     reached via `powercap_unregister_zone`.
   * `ib_destroy_cq_user` — `cq->wc` is freed in the driver's
     `ops.destroy_cq` callback.
7. **By-design partial-teardown APIs** — a function
   deliberately frees only the wrapper, leaving the payload
   to the caller (e.g. `free_vm_area` frees the `vm_struct`
   but not `area->pages`; freeing pages is `vfree`'s job).
8. **Cooperating / variant teardown** — a common helper that
   frees shared fields, called by variant destructors that
   free variant-specific fields first (e.g.
   `destroy_log_context` in dm-log handles the fields common
   to the core and disk log variants; `clean_bits` /
   `disk_header` are freed in `disk_dtr` before delegation).

These mechanisms dominate the surviving candidate set.  Of
the ~10 top-ranked candidates triaged in depth, every one
except `nx842_pseries_exit` was explained by mechanism 6, 7,
or 8.

## The one plausible real candidate: `nx842_pseries_exit`

File: `drivers/crypto/nx/nx-842-pseries.c` (renamed
`nx-common-pseries.c` in mainline).

```c
static void __exit nx842_pseries_exit(void)
{
    struct nx842_devdata *old_devdata;
    ...
    spin_lock_irqsave(&devdata_mutex, flags);
    old_devdata = rcu_dereference_check(devdata, ...);
    RCU_INIT_POINTER(devdata, NULL);          /* devdata nulled first */
    spin_unlock_irqrestore(&devdata_mutex, flags);
    synchronize_rcu();
    ...
    kfree(old_devdata);                        /* frees struct, NOT ->counters */
    vio_unregister_driver(&nx842_vio_driver);  /* triggers nx842_remove */
}
```

`old_devdata->counters` is a separate `kzalloc` (in the probe
path).  The matching device-remove callback `nx842_remove`
frees **both** `old_devdata->counters` and `old_devdata`.
But `nx842_pseries_exit`:

1. captures `devdata`, sets the global to NULL, frees
   `old_devdata` *without* freeing `->counters`;
2. then calls `vio_unregister_driver`, which triggers
   `nx842_remove` — but `devdata` is already NULL, so
   `nx842_remove` frees nothing.

So on the rmmod path with a bound device, `->counters`
appears to leak.  The asymmetry **persists unchanged from
linux_5_10 through current mainline** (verified in
`torvalds/linux.git` HEAD: `nx842_pseries_exit` still does
`kfree(old_devdata)` with no counters free).

**Assessment.** This is a *plausible* real leak, but:

* **Severity is low** — it leaks one small struct once, only
  on module unload, on a powerpc pseries crypto driver.
* **Confidence is moderate, not high** — the reasoning is
  static; I have not confirmed with kmemleak, nor fully ruled
  out a subtlety (e.g. whether `nx842_pseries_exit` ever runs
  with a device still bound in practice, or whether the
  ordering differs across kernel versions).

It is **not filed**.  It is recorded here as the single
candidate from this hunt that merits expert / dynamic
confirmation before any report.

## Conclusion

The bug-hunt did **not** yield a confidently-confirmed,
file-ready undisclosed bug from static analysis alone.  That
is an honest negative result, and it is informative:

* The destructor-completeness detector is **precise on the
  bug class it targets** (validated on two CVEs, 0 FP on the
  curated 29-sample), and the whole-tree scan is a strict
  superset that keeps detecting them.
* At kernel scale, the dominant obstacle is **indirect /
  callback-delegated frees** — the kernel frees most
  long-lived objects through registered `.release`/`.destroy`
  ops, which a purely static, intra-procedural-plus-named-
  helper analysis cannot follow.  Closing this gap needs an
  indirect-call / points-to layer (or the CBMC harness route,
  which models the allocations and lets symbolic execution
  decide).
* The exercise hardened the detector substantially (six FP
  classes fixed) and produced one candidate (`nx842`) worth a
  maintainer's eye.

## Artifacts

* Candidate CSVs: `/tmp/dtor-scan/linux_*.v5.csv` (88/85/80/79
  per tree; 49 common to all four).
* Detector + driver: `integration/linux/scan/
  destructor_completeness.py`, `destructor_scan.py`.

## Follow-ups

1. **Indirect-free awareness**: build an ops-table /
   `.release` callback index so callback-delegated frees are
   recognised.  This is the single biggest precision lever
   for whole-kernel destructor analysis.
2. **CBMC confirmation of `nx842`**: synthesise the
   init→exit harness modelling `counters`, and let CBMC
   decide whether the rmmod path leaks.
3. **kmemleak dynamic check** of `nx842` rmmod on a pseries
   target, if one is available, before any upstream report.

## Addendum: indirect-free awareness (release-callback index)

Implemented the top follow-up (#1 above).  `_build_release_freed`
collects functions assigned to a release-ops field
(`.release`/`.destroy`/`.free`/`.dtor`/`.remove`/...) anywhere
in the tree, records the struct fields each disposes
(`kfree(v->f)` or `v->f = NULL`), and subtracts them from the
missing set by struct type.

Effect on linux_5_10: candidates **88 → 66** (release-callback
delegation pruned 22 more FPs, e.g. `rapl_remove_package`).
The radeon CVE is preserved (its `iio` free is in
`atom_destroy`, a directly-called helper, not a registered
callback).

Note this also (correctly per the detector's syntactic model)
prunes `nx842_pseries_exit`: `counters` IS freed in the
`.remove` callback `nx842_remove`; the rmmod-ordering bug that
defeats that free is an interprocedural ordering property
beyond syntactic field-set analysis.  With the callback index,
the detector no longer flags it — the honest consequence is
that this one subtle candidate now requires
ordering/path-sensitive reasoning (or dynamic kmemleak) to
surface, which the static detector deliberately does not
attempt.
