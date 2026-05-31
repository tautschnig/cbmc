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

## Results: per-tree breakdown (seed 42)

60 rows on this seed.  Per-tree verdict distribution:

| Tree | candidate | fp-filtered | successful | vacuous | noise | error | skip+timeout |
|---|---:|---:|---:|---:|---:|---:|---:|
| linux_5_10 | 0 | 1 | 0 | 0 | 1 | 9 | 1 |
| linux_6_1  | 0 | 1 | 0 | 0 | 3 | 11 | 2 |
| linux_6_6  | 0 | 1 | 0 | 0 | 0 | 13 | 3 |
| linux_6_12 | 0 | 1 | 0 | 1 | 1 | 9 | 2 |

The detection-row count is 0 across all trees because the
sample (seed 42) didn't pick any of the 10 known-detected
CVEs (those are sparse in the random sample of 30 from
~3000+ CVEs).

## Cross-tree consistency

For each `(CVE, module)` pair on the seed-42 sample, the
verdicts across trees were aggregated:

| Outcome | Count of pairs |
|---|---:|
| Consistent (same verdict on all trees) | 24 / 27 |
| Inconsistent across trees | 3 / 27 |

The 3 inconsistent pairs were ALL `error` ↔ `timeout`
divergences — tree-specific compile-stack differences (e.g.
the file compiles in 5.10 and times out in cbmc symex; the
same file errors at compile in 6.6 because of a missing
header).  None of the inconsistencies were catalog-level
disagreements (verdicts that the catalog itself produced
differently across trees).

**Conclusion:** the catalog's verdicts are consistent across
LTS branches for every case where the file compiles cleanly
in multiple trees.  Tree-specific divergence is dominated by
compile-stack / config drift, not by catalog behaviour.

## Module-pick correctness — issue surfaced

Multi-LTS validation surfaced an issue that wasn't visible
in single-tree runs: the catalog's module-picker can pick
the wrong module for a CVE, and the post-pick triage filter
(when not module-aware) over-suppresses:

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
module is in the shape's applicability set.  An over-
suppression bug for non-leak CVEs in the older filter has
been fixed.

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
