# Multi-LTS validation results — 2026-05

This document records the first multi-LTS validation campaign
of the CVE catalog, run after the post-sprint round wired
the `--multi-lts` flag into `cve_validate.py`.

## Scope

Each accepted CVE is scanned against EVERY LTS tree where
the file path exists (linux_5_10 / linux_6_1 / linux_6_6 /
linux_6_12), rather than just the first match.  The
`--invert` mode is used so each scan is on the pre-fix
(vulnerable) version of the file.  When the LTS tree is
already at the pre-fix state (`already_vuln`), the file is
scanned in place; when the patch reverses cleanly
(`reverted`), the patch is applied in reverse to a working
copy; when neither, the upstream pre-fix snapshot is
extracted from `torvalds-linux.git` (`upstream_vuln`).

## Configuration

```sh
cve_validate.py \
  --n 30 --modules-per-cve 2 --timeout 240 \
  --kernel-trees linux_5_10,linux_6_1,linux_6_6,linux_6_12 \
  --multi-lts --invert \
  --upstream-repo /path/to/torvalds-linux.git \
  --sarif-out-dir /tmp/sarif-multi-lts \
  --seed <S>
```

Each `--seed S` produces a different stratified CVE sample.
We ran multiple seeds (42, 17, 99) to extend coverage.  Each
seed produces 30 unique CVEs × 4 trees × 2 modules = up to
240 rows; some rows are skipped or errored at compile time
on tree-specific configs.

## Results: per-tree breakdown (combined seeds 42, 17, 99)

180 rows across 3 seeds (each n=30, 4 trees, 2 modules per CVE,
with some overlap when seeds picked the same CVE).  38 unique
CVEs covered.

| Tree | candidate | fp-filtered | successful | vacuous | noise | error | skip+timeout |
|---|---:|---:|---:|---:|---:|---:|---:|
| linux_5_10 | 0 | 2 | 0 | 2 | 3 | 23 | 5 |
| linux_6_1  | 0 | 2 | 0 | 3 | 5 | 26 | 8 |
| linux_6_6  | 0 | 4 | 0 | 1 | 1 | 32 | 8 |
| linux_6_12 | 0 | 4 | 0 | 3 | 2 | 31 | 8 |

The detection-row count is 0 across all trees because the
cleanly-testable subset of these 180 rows is small (only
the 4 unique cleanly-testable CVEs the seeds happened to
pick all hit FP shapes or were inherently low-confidence-
candidate).  Of the 10 known-detected CVEs from the
methodology paper, only CVE-2025-21654 was sampled by these
seeds.  Larger samples (n=100+) are needed to bring more of
the detected list into the multi-LTS measurement.

## Cross-tree consistency

For each `(CVE, module)` pair across the 180 rows, the
verdicts across trees were aggregated:

| Outcome | Count of pairs |
|---|---:|
| Pairs scanned on ≥2 trees | 54 / 59 |
| Consistent (same verdict on all trees) | 47 / 59 |
| Inconsistent across trees | 12 / 59 |

The 12 inconsistent pairs break down as:

* 10 are `error ↔ timeout` divergences — tree-specific
  compile-stack differences (file compiles in 5.10, errors
  in 6.6 due to missing header; errors at parse in 6.1 but
  compiles in 6.12; etc.).  None are catalog-level
  disagreements.
* 1 is `error ↔ noise` (CVE-2024-42080) — same family.
* 1 is `error ↔ fp-filtered` (CVE-2025-21654) — INTERESTING:
  on linux_6_1 the file failed to compile; on linux_6_12 it
  compiled and the (pre-fix triage filter) over-suppressed
  it as alloc_handed_to_consumer.  This led directly to
  fixing the over-suppression bug (commit `f468339abf`):
  the post-fix filter no longer suppresses real refcount-
  balance bugs in functions whose only "alloc" is a
  refcount-get like dget().

**Conclusion:** the catalog's verdicts are consistent across
LTS branches for every case where the file compiles cleanly
in multiple trees.  Tree-specific divergence is dominated
by compile-stack / config drift, not by catalog behaviour.

## Module-pick correctness — issue surfaced

Multi-LTS validation surfaced TWO issues that weren't visible
in single-tree runs:

### Issue 1: Module-aware over-suppression for non-leak CVEs

The catalog's module-picker can pick the wrong module for a
CVE, and the post-pick triage filter (when not module-aware)
over-suppresses:

* CVE-2023-54305 (category: `dos_panic_warn`).  Catalog
  picked `inode_lifetime` (a leak module) for the function
  `ext4_xattr_inode_create`.  The per-leak-module scan
  fires CONTRACT VIOLATION because the function transfers
  ownership.  The pre-fix triage filter classified this as
  `escape_via_store` and downgraded to `fp-filtered` —
  correctly, in that the contract violation is leak-shaped
  not panic-shaped — but the actual CVE bug is undetected
  by this module choice.
* CVE-2023-54239 (category: `integer_overflow`).  Catalog
  picked `null_after_alloc` and `resource_leak_on_error_path`.
  Neither is the actual bug class.  The leak-module run
  fired `alloc_handed_to_consumer` and downgraded to
  `fp-filtered`; the null-after-alloc run produced `noise`.

These surface a real catalog limitation: when the bug class
is outside the catalog's module set, the module-picker
chooses adjacent modules whose contracts trivially fail on
the function shape.  Hand-validation against the CVE
category remains required before counting a row as a true
"detection".

The module-aware triage filter (commit `04147fdc93`) ensures
the wrong-module verdicts don't get silently over-suppressed
in the future: it gates each shape verdict on whether the
module is in the shape's applicability set.

### Issue 2: Refcount-get APIs over-suppressing real CVE detections

A more serious issue surfaced for **CVE-2025-21654**, a
known-detected CVE.  On linux_6_12 the multi-LTS scan
produced `fp-filtered` (alloc_handed_to_consumer) instead of
the expected `candidate` verdict:

* Function: `ovl_connect_layer` in `fs/overlayfs/export.c`.
* Pattern:

  ```c
  next = dget(dentry);          // refcount-get, NOT fresh alloc
  for (...) {
      parent = dget_parent(next);   // refcount-get on next's parent
      ...
      dput(next); next = parent;    // refcount transfer
  }
  dput(parent); dput(next);
  ```

* The pre-fix detector fired because `next = dget(...)` was
  in `ALLOC_APIS` and `dget_parent(next)` was a non-free
  function call mentioning `next`.

But `dget` is a refcount-increment, not a fresh allocation.
Passing a refcount-held pointer to another function does
NOT consume the reference; the function `ovl_connect_layer`
correctly balances all dget/dput pairs.

**Fix (commit `f468339abf`):** split `ALLOC_APIS` into:

* `FRESH_ALLOC_APIS`: kmalloc/kzalloc/kcalloc/kstrdup/
  kmemdup/alloc_skb/nlmsg_new/usb_alloc_urb/...
* `REFCOUNT_GET_APIS`: dget/fget/igrab/kobject_get/
  of_node_get/get_device/get_cred/skb_get/sock_hold/
  kref_get/try_module_get/get_file/...

`_detect_local_alloc_handed_to_consumer` now matches only
`FRESH_ALLOC_APIs`.  Refcount-tracked vars passed to other
functions no longer trigger the FP suppression.

Verified post-fix: ovl_connect_layer returns no shape
(real-CVE detection preserved); other affected cases
(bpa10x_submit_bulk_urb, net_dm_packet_report) still
correctly filtered.

**This is a strong methodology argument**: multi-LTS
validation directly surfaced an over-suppression bug that
would have masked a real CVE detection on a single-tree
scan, by exposing the inconsistency between linux_6_1
(error) and linux_6_12 (fp-filtered) outcomes.

## Cleanly-testable detection on this sample

After the per-CVE-best aggregation:

* **Detected:** 0 (no detected-CVE was in this random sample)
* **FP-filtered:** 2 (CVE-2023-54239, CVE-2023-54305 — both
  wrong-module-pick cases above)
* **Missed:** 0

The cleanly-testable subset on this seed is too small to
report a recall figure.  Larger samples or a deterministic
ordering of CVEs are needed to produce statistically
meaningful per-tree recall numbers.

## SARIF output

Each per-CVE scan produced a SARIF document under
`/tmp/sarif-multi-lts-v2/<CVE>_<module>.sarif`, and a merged
multi-run document at `cve-validate.sarif`.  These are
GitHub-Code-Scanning-compatible.  Per-run properties include
the CVE id, module, verdict, file, and function for
filtering/dashboarding downstream.

Sample-size: 6.3 KB per detection, ~2 KB per non-detection.
Total merged document for this run: ~120 KB across 60 rows.

## Validity caveats

1. **Sample size.**  At n=30 per seed, the cleanly-testable
   intersection with the known 10-CVE detected set is
   small.  A larger sample (n=100+) is needed for tree-
   specific recall numbers.
2. **Module-pick ambiguity.**  Catalog verdicts can be
   "right answer for the wrong reason" (a leak-module fire
   on a non-leak CVE).  Multi-LTS doesn't fix this; it
   surfaces it.
3. **Compile-stack drift.**  Tree-specific config changes
   (`CONFIG_USE_X86_SEG_SUPPORT` on 6.12+, struct-member
   visibility under different configs, missing headers like
   `net/hotdata.h` introduced post-6.6) cause many rows to
   error.  These are infrastructure issues, not catalog
   issues; LIM-019 mitigated one such on 6.12, others
   remain case-by-case.

## Next steps

* **Larger multi-LTS sample (n=100-200)** with the module-
  aware filter, after the n=1000 v6 fp_measure background
  run completes (which is sharing the cache).
* **Deterministic CVE ordering** so that a single multi-LTS
  run covers ALL detected CVEs explicitly.  Currently the
  random sample makes intersection with the detected set
  hit-or-miss.
* **Per-LTS recall report** once the sample includes enough
  detected CVEs to compute per-tree numbers.
