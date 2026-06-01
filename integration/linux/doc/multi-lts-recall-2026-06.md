# Targeted multi-LTS recall measurement and triage closeout — 2026-06

This documents the targeted multi-LTS recall measurement on
the 10 detected CVEs from the methodology paper, the
hand-triage of 3 still-unfiltered candidates from the n=1000
v6 run, and the resulting filter fixes.

## Headline findings

1. **Targeted multi-LTS recall: 5/10 cleanly-testable CVEs
   detected on at least one LTS tree.**  Down from the
   methodology paper's claim of 9-10/10.  The regression is
   attributable to two issues this measurement surfaced and
   one structural limitation:

   - **Filter regression 1: leak-shape over-suppression on
     non-leak modules.**  `escape_via_store` /
     `alloc_handed_to_consumer` were treating refcount-get
     APIs (`dget`, `kref_get`, ...) as fresh allocations.
     CVE-2025-21654 was masked.  Fixed in commit `f468339abf`
     by separating ALLOC_APIS into FRESH vs REFCOUNT_GET.

   - **Filter regression 2: leak-shape over-suppression on
     multi-alloc functions.**  Functions with multiple alloc
     sites (e.g. lima_heap_alloc has both kvmalloc_array
     and shmem_read_mapping_page in a loop) were being
     suppressed by the ownership-transfer detection on the
     FIRST alloc, hiding real leaks on OTHER allocs.
     CVE-2024-35829 and CVE-2026-43304 were masked.  Fixed
     in commit `66a0011c39` by adding a single-alloc gate
     to `_detect_alloc_into_param_field`,
     `_detect_local_alloc_handed_to_consumer`, and
     `_detect_param_consumed_by_callee`.

   - **Filter regression 3: ownership-handler suppression
     on cleanup-function leak bugs.**  Cleanup functions
     (`*_fini`, `*_cleanup`, `*_destroy`, ...) routinely
     have missing-kfree bugs IN them; the
     `ownership_handler` shape was suppressing those
     legitimate bug detections.  Fixed in commit
     `25d7549b86` by adding
     `OWNERSHIP_HANDLER_NEGATIVE_GATE = LEAK_MODULES`:
     ownership_handler now applies everywhere EXCEPT leak
     modules.

   - **Structural limitation: cross-function leak detection.**
     The remaining 3 CVEs marked `vacuous` (CVE-2023-53453,
     CVE-2023-53697, CVE-2024-39492) all have the same
     shape: a constructor function (e.g.
     `radeon_atombios_init`) allocates a resource, and a
     bug-prone cleanup function (e.g.
     `radeon_atombios_fini`) is missing a kfree.  Detecting
     this requires the harness to chain
     `init() -> fini()` so that the GLOBAL ghost is
     populated by init's alloc and the leak assertion at
     fini's exit fires.  scan-per-file's per-function
     harness only calls the target function, so init's
     alloc is never tracked.  This was not a regression
     from this measurement — the prior methodology paper
     measurement either had a different harness setup, or
     the "via fallback" detection claimed for these CVEs
     was overstated.

2. **Hand-triage of n=1000 v6 unfiltered candidates: 3/3 are
   FPs from cocci CFG limitations.**  Zero real undisclosed
   bugs.

   - `counter_signal_ext_register`: loop-allocate-and-register
     pattern.  The cocci's UAF detector tracks
     `signal_ext_comp` across loop iterations even though
     the variable is reassigned each iteration.  Same family
     as `process_return_queue` from the n=500 v3 long tail.
   - `cc_cipher_process`: complex crypto request processor.
     The cocci's null_after_alloc fires on
     `req_ctx->iv = kmemdup(...)` without recognizing the
     `if (!req_ctx->iv) goto exit_process` null check,
     because `req_ctx` is derived via `skcipher_request_ctx(req)`
     and the cocci can't trace the alias.
   - `wilc_wfi_mgmt_tx_complete`: tx completion callback
     that frees `pv_data->buff` then `pv_data`.  The cocci
     fires on the kfree-then-kfree sequence, mistakenly
     treating the second kfree as a use-after-free.

3. **Per-tree consistency**: the 5 detected CVEs that DO
   produce candidate verdicts produce the same verdict on
   every LTS tree where the file compiles cleanly:

   | CVE | 5_10 | 6_1 | 6_6 | 6_12 |
   |---|---|---|---|---|
   | CVE-2024-35829 | candidate | candidate | candidate | candidate |
   | CVE-2024-43818 | — (not in tree) | candidate | candidate | candidate |
   | CVE-2025-21654 | error | error | candidate | candidate |
   | CVE-2025-40307 | error | candidate | candidate | candidate |
   | CVE-2026-43304 | candidate | candidate | candidate | candidate |

   Tree-specific divergence is dominated by compile-stack
   differences (file compiles in 6_6/6_12 but errors in
   5_10/6_1 due to header drift).

## Per-tree recall

| Tree | Detected | FP-filtered | Cleanly-tested |
|---|---:|---:|---:|
| linux_5_10 | 2 | 1 | 3 |
| linux_6_1  | 4 | 3 | 7 |
| linux_6_6  | 5 | 2 | 7 |
| linux_6_12 | 5 | 2 | 7 |

linux_6_6 and 6_12 are the most consistent test trees on
this CVE set.

## Driver and reproduction

The driver is at
`integration/linux/doc/scripts/cve_recall_multi_lts.py`.
It hard-codes the 10 detected CVEs (with file/function/modules
for each) and runs cve_validate's `_run_scan` per
`(CVE × LTS-tree-where-file-exists × candidate-modules)`
combination.  Each module is tried independently; the
per-(CVE, tree) best-verdict is reported.

Example invocation:

```sh
python3 integration/linux/doc/scripts/cve_recall_multi_lts.py \
  --out-dir /tmp/cve-recall \
  --sarif-out-dir /tmp/sarif-recall \
  --timeout 300
```

## Methodology paper revision needed

The methodology paper's claim of "10 detected CVEs / 100%
recall" should be updated to reflect:

* **5/10 detected** with the current filter (cleanly-testable
  subset, multi-LTS scan against linux_5_10/6_1/6_6/6_12).
* The 5 NOT detected today fall into two pattern families:
  - 3 cleanup-function leaks (CVE-2023-53453, CVE-2023-53697,
    CVE-2024-39492) where the bug requires init→fini
    chaining in the harness.  Listed as "structural
    limitation, not regression" — the catalog could detect
    these with a different harness shape, but the
    per-function harness can't.
  - 2 timeouts (CVE-2023-53038, CVE-2025-21895) where CBMC
    takes >300s.  Tunable per-CVE unwind / unwind-budget
    might unstick.

## Filter recall + FP rate trade-off summary

After all post-sprint changes:

| Measurement | Pre-sprint | Mid-sprint | Post-multi-LTS |
|---|---:|---:|---:|
| Recall on 10 CVEs (any tree) | 9-10/10 (claimed) | 2/10 (over-suppressed) | **5/10 (verified)** |
| n=500 v3 long-tail filter rate | 0/49 | 45/49 | 38/49 |
| n=1000 v6 candidates after filter | (no data) | 3/315 | (would be ≥3/315, slightly more) |

The post-multi-LTS rate is the trade-off that respects both
goals: real CVEs aren't over-suppressed, and most FPs are
still filtered.

## Commits in this round

```
25d7549b86 linux: triage filter — ownership_handler doesn't apply to leak modules
66a0011c39 linux: triage filter — single-alloc gate fixes recall regression on lima_heap_alloc / set_secret
f468339abf linux: split alloc-APIs into FRESH vs REFCOUNT_GET; fix CVE-2025-21654 over-suppression
04147fdc93 linux: module-aware triage filter — prevent over-suppression of non-leak CVEs
```

(Plus the targeted recall driver: this commit adds
`integration/linux/doc/scripts/cve_recall_multi_lts.py`.)

## Open follow-ups

1. **Cross-function leak harness**.  CVE-2023-53453 family
   needs an init→fini chained harness.  The
   `cross-function-uaf-design-2026-05.md` document discusses
   a similar Phase 2 facility for UAF; extending it to leak
   detection would close 3 CVEs.

2. **Timeout investigation**.  CVE-2023-53038 and
   CVE-2025-21895 hit 300s timeouts.  Per-CVE tunable
   unwind / sliced harness might help.

3. **`_complete` callback infix**.  Adding it to
   ownership_handler infix list would suppress
   `wilc_wfi_mgmt_tx_complete` and similar.  Risk: many
   `_complete` functions aren't pure cleanup.  Needs a
   broader survey.

4. **Cocci CFG limitations**.  Loop-reassign and
   alias-tracing remain the recurring cocci limitations.
   A more path-sensitive cocci pass or postprocess
   transformation could close the remaining n=1000 long
   tail.
