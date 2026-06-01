# Destructor-completeness detector — 2026-06

This documents the cross-function leak detector built to
close the recall gap surfaced by the targeted multi-LTS
measurement (`multi-lts-recall-2026-06.md`).

## Motivation

The multi-LTS recall measurement showed the catalog detected
only 5/10 of the cleanly-testable CVEs.  Three of the misses
shared a shape the per-function harness structurally cannot
catch: an **incomplete destructor** — a function that tears
down a heap object but forgets to free one of the struct's
owned heap-pointer fields, where the allocation site is in a
different function (often a different file).

Deep-dive findings:

* **CVE-2023-53453** (`radeon_atombios_fini`): the struct
  `atom_context` has heap fields `iio` and `scratch`.
  `radeon_atombios_fini` frees `scratch` + the struct but not
  `iio`.  `iio` is allocated in `atom.c` (`atom_index_iio`)
  and freed by the *sibling redundant destructor*
  `atom_destroy` (also in atom.c).
* **CVE-2023-53697** (`unregister_nvdimm_pmu`): frees `nd_pmu`
  but not `nd_pmu->pmu.attr_groups`, which the *matching
  constructor* `register_nvdimm_pmu` allocates.
* **CVE-2024-39492** — turned out NOT to be a memory leak at
  all.  The fix is `WARN_ON(pm_runtime_get_sync(...))` →
  `WARN_ON(pm_runtime_get_sync(...) < 0)`, a spurious-warning
  fix.  The CVE survey miscategorized it as `resource_leak`.
  It is correctly NOT detected by any leak module and should
  be excluded from the leak-detectable denominator.

So the genuine target was CVE-2023-53453 + CVE-2023-53697.

## The detector

`integration/linux/scan/destructor_completeness.py`.

Static, cross-function, multi-file analysis:

1. Parse the destructor D.  Find the *primary object* O (the
   expression that is itself `kfree`'d, identified as the
   prefix of a field-free), and the fields D frees directly.
2. Add **transitively-freed** fields: for each helper D calls
   with O as an argument, the fields that helper frees on O.
   This avoids false positives when a destructor delegates
   field teardown to a helper (e.g. `null_del_dev` calls
   `cleanup_queues(nullb)` which frees `nullb->queues`).
3. Resolve O's struct type T (terminal-field declaration, or
   the destructor's parameter type — anchored on the function
   *definition*, not a call site).
4. Compute the **owned-field set** from *precise* cross-
   function evidence only:
   * **redundant sibling destructors** — a single function
     that both bare-frees a `T *` variable AND frees its
     fields (per-function, so cooperating split teardowns
     like configfs `drop_item`/`release` are NOT conflated),
   * the **name-paired constructor**'s field allocations
     (`fini↔init`, `unregister↔register`, `destroy↔create`,
     `free↔alloc`, …).
5. `missing = owned - freed`.  Report a leak candidate when
   D frees the struct object itself and `missing` is
   non-empty.

### Why the precise owned-field signal matters

An earlier version used "any function touches field f of
type T" as the owned signal.  That over-reported badly
(qlcnic, k90, null_del_dev all flagged).  The precise signal
— *redundant sibling destructor* OR *name-paired constructor*
— requires evidence that the field participates in the **same
object lifecycle** as D, which is exactly the condition under
which D is responsible for freeing it.

The "per-function, must also free the struct itself"
requirement distinguishes:

* **redundant** destructors (atom_destroy frees `ctx` AND
  `ctx->iio`) — alternative complete teardowns that should
  agree on the field set; disagreement ⇒ bug.
* **cooperating** destructors (configfs `drop_item` frees
  only `->name`, `release` frees only the struct) — split
  teardown that legitimately frees disjoint sets; must NOT
  be treated as disagreement.

## Validation

| Case | State | Result |
|---|---|---|
| radeon_atombios_fini | vuln (5.10) | MISSING `iio` ✓ |
| radeon_atombios_fini | fixed (6.12) | complete ✓ |
| unregister_nvdimm_pmu | vuln | MISSING `pmu.attr_groups` ✓ |
| unregister_nvdimm_pmu | fixed | complete ✓ |

**False-positive corpus check**: 0 false positives across the
29 full destructors sampled from the n=1000 v6 run, including
the previously-tricky `k90_cleanup_macro_functions`,
`clean_via_table`, `null_del_dev` (helper-delegated free),
`qlcnic_82xx_free_mac_list` (frees list entries, not the
adapter), `fuse_file_free`, and the usb-gadget configfs
two-phase teardown.

## CBMC-verified form (prototype)

The detector is static, but the same property is CBMC-
verifiable, and a prototype confirms it:

```c
void radeon_atombios_fini_dtor_harness(void) {
    struct radeon_device *rdev = <nondet>;
    __CPROVER_assume(rdev->mode_info.atom_context != 0);
    /* model the owned-field allocs with DISTINCT pointers */
    __CPROVER_assume(ctx->iio != 0 && ctx->scratch != 0 &&
                     ctx->iio != ctx->scratch && ...);
    leak_alloc_track(ctx->iio);
    leak_alloc_track(ctx->scratch);
    leak_alloc_track(ctx);
    radeon_atombios_fini(rdev);
    __assert_no_outstanding_leak();   /* FAILs on vuln, holds on fix */
}
```

Verified: the contract precondition FAILs on the vulnerable
body (iio leaked) and SUCCEEDs on the fixed body.  The
distinctness assumptions are required because the leak ghost
keys on pointer value; without them CBMC could alias the
fields.  Wiring this harness synthesis into `scan-per-file`
(discovering the owned fields via the static detector, then
generating the assume-distinct + track preamble) is future
work — the static detector already provides the detection
signal for the catalog.

## Integration

`cve_validate._run_scan` runs the analyzer on the `vacuous`
path (rc==12) for leak-class modules
(`_LEAK_MODULES_FOR_DTOR`).  A `vacuous` verdict means the
per-function harness saw no contract clause — exactly what
happens for a cross-function leak where the alloc is
elsewhere.  When the analyzer reports missing fields, the
verdict is upgraded to `candidate`.

## Recall impact

Targeted multi-LTS measurement on the 10 detected CVEs:

| | Pre-dtor | Post-dtor |
|---|---:|---:|
| Detected (≥1 tree) | 5/10 | **7/10** |
| Excluding miscategorized CVE-2024-39492 | 5/9 | **7/9 = 78%** |

Per-tree (post-dtor):

| Tree | Detected | FP-filtered | Cleanly-tested |
|---|---:|---:|---:|
| linux_5_10 | 4 | 1 | 5 |
| linux_6_1  | 7 | 3 | 10 |
| linux_6_6  | 6 | 2 | 8 |
| linux_6_12 | 6 | 2 | 8 |

linux_6_1 detects all 7 (the file states are cleanly testable
there for the most CVEs).

## Commits

```
30f23b09df linux: destructor-completeness detector for cross-function leaks
b2de4351ad linux: remove dead _owned_fields_of_type from destructor_completeness
6789f644be linux: wire destructor-completeness into cve_validate vacuous path (recall 5/10 to 7/10)
```

## Open follow-ups

1. **Harness-synthesis automation** for the CBMC-verified
   form, so detector candidates are confirmed by CBMC rather
   than reported from static analysis alone.
2. **Timeout CVEs** (CVE-2023-53038, CVE-2025-21895):
   per-CVE unwind / slicing.
3. **CVE survey hygiene**: CVE-2024-39492 is miscategorized as
   `resource_leak`.  A pass over the survey's category labels
   would tighten the recall denominator.
4. **New-bug potential**: the detector flagged 4 candidates in
   the n=1000 corpus before the precise-signal tightening; all
   were FPs (cooperating teardowns / aliasing).  Re-running
   the tightened detector over a larger corpus is a bug-hunt
   opportunity — destructor asymmetry is a real, common bug
   class.
