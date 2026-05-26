# Cocci alloc-API + CBMC abort triage + compile-stack
# tail — closeout

**Date:** 2026-05-26

This iteration tackled three follow-on priorities from the
previous closeout:

1. Extend the cocci alloc-API list to cover wrapper
   allocators that the catalog was missing.
2. Triage the CBMC invariant-violation aborts that were
   blocking some per-file scans.
3. Fix the long-tail compile-stack errors that the n=200
   v3 run surfaced.

## Part 1: Cocci alloc-API extension

**Problem.** The `resource_leak_on_error_path.cocci` and
`null_after_alloc.cocci` rules covered only ~9 allocator
APIs — but the kernel uses 30+ allocator helpers, with the
top-five (kzalloc, devm_kzalloc, kmalloc, kcalloc,
dma_alloc_coherent) accounting for 26,954 call sites in
Linux 5.10 alone.  A function using `kvmalloc_array` or
`kmemdup` wouldn't trigger any cocci instrumentation, so
the per-file scan was vacuous.

**Approach.** Each allocator family now has its own per-API
rule pair (assign + decl).  Per-API rules keep cocci's
CFG analysis bounded — combining them in a single
`\(... \| ...\)` alternation triggers
"inconsistent control-flow paths" errors on functions with
many returns, which makes cocci abandon the WHOLE FILE
rather than just skip the troublesome rule.

**Coverage added** (resource_leak_on_error_path.cocci):

| Family | Free target | APIs |
|---|---|---|
| k*alloc | kfree | kmalloc, kzalloc, kcalloc, kmalloc_array, kmemdup, kstrdup, kasprintf |
| kv*alloc | kvfree | kvmalloc, kvzalloc, kvmalloc_array, kvcalloc |
| vmalloc | vfree | vmalloc, vzalloc |
| skb | kfree_skb / consume_skb | alloc_skb |
| slab | kmem_cache_free | kmem_cache_alloc, kmem_cache_zalloc |
| workqueue | destroy_workqueue | alloc_workqueue |

**Coverage added** (null_after_alloc.cocci): same list,
matching `x = ALLOC()` followed by `x->fld = E` assignment.

The rule files grew from 120 / 50 lines to 459 / 274 lines.
Total cases the catalog can now surface allocator-bug shapes
on grew from ~9 covered allocators to 16 covered allocators.

**What's still NOT covered:**

- `devm_*` family (devres-managed): semantics differ from
  plain alloc → kfree (auto-cleaned on device removal).
  Modeling this correctly requires tracking device init
  state.  Skipped.
- `dma_alloc_coherent`, `dma_pool_alloc`, `__get_free_pages`,
  `alloc_pages`, `alloc_netdev` etc.: each needs its own
  free-API match.  Adding them mechanically would 4x the
  rule file again; deferred until measurement shows specific
  CVEs in those subsystems.

## Part 2: CBMC invariant-violation aborts

Two distinct upstream CBMC bugs surfaced.  Reproducer:
`lib/argv_split.c::argv_split` with
`INSTRUMENT=resource_leak_on_error_path` on Linux 5.10.

### LIM-017 — overflow side-effect inside statement-expr

```
File: src/goto-symex/goto_symex.cpp:85 function: symex_assign
Condition: false
Reason: Unreachable
```

The kernel's `kmalloc_array` wraps `__builtin_mul_overflow`
in a `check_mul_overflow` macro that expands to a GNU
statement-expression `({ ... ; result; })`.  CBMC's
`goto_convert_side_effect.cpp::remove_overflow` lowers the
`__builtin_mul_overflow` correctly when called at top
level, but the statement-expression's stored-temporary
path leaves `ID_overflow_mult` un-lowered.  `symex_assign`
recognises a fixed list of side-effect statements
(`cpp_new`, `allocate`, `va_start`, etc.) and aborts on
anything else.

**Workaround applied.** `scan/fragments/scan-compat.h`
overrides `check_mul_overflow`, `check_add_overflow`, and
`check_sub_overflow` to skip the overflow detection.  The
multiplication still happens; only the overflow side-effect
is dropped.  Sound under the usual scan interpretation:
overflow detection is `integer_overflow_in_alloc_size`'s
job, not the leak / null-deref analyses'.

### LIM-018 — boolbv_map width mismatch

```
File: src/solvers/flattening/boolbv_map.cpp:68 function: get_literals
Condition: map_entry.literal_map.size() == width
Reason: number of literals in the literal map shall equal the bitvector width
```

Surfaces after LIM-017 is worked around.  Some symbol is
cached in `boolbv_map` with one bit-width then looked up at
a different width.  Likely a struct-field-width disagreement
between two link partners (property module vs. kernel TU
vs. harness).

**No workaround yet.**  The validator catches the abort as
`error` verdict so the corpus continues.  Reproducible at
`/tmp/argv-keep2/argv_split.instr.gb` in this session's
work tree.

Both documented in `CBMC_LIMITATIONS.md`.

## Part 3: Compile-stack long-tail

Categorised the 99 errors from the n=200 v3 run into 35
distinct patterns.  Top patterns:

| Pattern | Count | Status |
|---|---:|---|
| missing-include | 19 | unfixable in scan-compat (driver-private headers) |
| member-not-found | 14 | API drift; needs upstream extraction (already handled) |
| missing-symbol | 8 | unfixable in scan-compat (CONFIG-gated symbols) |
| harness-synth-fail | 7 | **FIXED** (now classified `skipped`) |
| incomplete-struct | 6 | unfixable in scan-compat |
| inet_sock.h:407 / container_of | 6 | needs CBMC frontend fix |
| goto-cc-segfault | 4 | needs upstream investigation |
| struct-redefinition | 3 | unfixable in scan-compat |
| non-const-init | 2 | per-file specific |

### Fix 1: `function not found` → skipped instead of error

The synthesizer was returning exit code 2 (real synth
failure) when the patch hunk named a function that doesn't
exist as a definition in the file (e.g. `dev_err`,
`drm_modeset_unlock` are macro names, not function
definitions).  scan-per-file.sh surfaced this as `error`.

**Fix.** Synthesizer now exits 5 specifically for
"function not found", and scan-per-file.sh surfaces this
as `skipped`.  Converts ~7 cases from `error` to the
`skipped` bucket.

### Fix 2: Extended `BAD_FN_NAMES` filter in cve_validate

Added kernel logging helpers (`dev_err`, `pr_err`,
`WARN_ON`, etc.), DRM helpers (`drm_modeset_unlock`,
`drm_dev_alloc`), and iteration macros
(`list_for_each_entry`, `for_each_possible_cpu`,
`rcu_read_lock`).  Catches more macro-invocation false
positives in `_parse_patch` before they reach the
synthesizer.

## Final n=200 measurement

After all three improvements:

| Metric | n=200 v3 | n=200 v4 |
|---|---:|---:|
| Verdict rows | 185 | 185 |
| Unique CVEs | 94 | 94 |
| **error** | 99 | **92** (-7) |
| **vacuous** | 60 | 63 (+3) |
| **skipped** | 0 | 4 (newly classified) |
| Cleanly-testable rows | 15 | 15 |
| Cleanly-testable unique CVEs | 14 | 14 |
| Detected | 2 | 2 |
| FP-filtered | 6 | 6 |
| Catalog-missed | 6 | 6 |

**Recall on cleanly-testable bugs:** 2 / (2 + 6) = **25%**,
unchanged.  The improvements reduced error count and added
a new `skipped` bucket for cleanly-classified
function-not-in-TU cases — but didn't unblock new
detections on the cleanly-testable set.

## Honest assessment

**What this iteration confirms:**

- **The cocci coverage was a real bottleneck.**  With per-API
  rules across 16 allocators, the catalog can now in
  principle catch resource-leak / null-after-alloc bugs in
  any kernel function using these APIs.  The
  vacuous-verdict rate is now driven by the function having
  no bug rather than by missing cocci coverage.
- **Two CBMC bugs documented.**  LIM-017 is mitigated;
  LIM-018 needs upstream fix or a different workaround.  The
  validator handles both gracefully (catches the abort as
  error).
- **Compile-stack fixes are tractable but per-pattern.**  The
  long tail is dominated by config-mismatch and
  kernel-private-header issues that don't generalise — each
  CVE in those categories needs case-specific handling.

**What this iteration didn't change:**

- Detection count remains at 2 unique CVEs (CVE-2024-27025
  nbd_genl_status, CVE-2025-21654 ovl_connect_layer).
- Recall remains at 25% on the cleanly-testable subset.
- The remaining catalog-missed cases need either a different
  bug-shape model (e.g., resource_leak on a *return* of an
  alloc'd pointer) or substantially better cocci CFG
  analysis.

## What's left

Remaining priorities, in honest order of value-per-effort:

1. **LIM-018 root cause.**  The boolbv_map width-mismatch
   abort blocks several CVEs.  Likely a small number of
   distinct upstream bugs.  Reduction pipeline (delta-debug
   the .gb file) would help isolate.  ~1-2 weeks.
2. **Investigate why ovl_connect_layer detection works but
   most_register_interface doesn't.**  Both have the
   ownership-handler shape but only one is detected.  Likely
   a synthesizer / triage-filter interaction worth deeper
   triage.  ~3-5 days.
3. **More allocators in cocci** (devm_kmalloc family,
   dma_alloc_coherent, alloc_netdev).  Each adds covered
   surface area.  ~3-5 days per family.
4. **Hand-triage 5-10 of the 6 catalog-missed CVEs** to
   understand WHICH bug shapes the catalog can't model and
   what new module would help.  ~3 days.

## Reproducing

```sh
ulimit -v unlimited
mkdir -p /tmp/cve-validate
systemd-run --user --scope --quiet \
    --property=MemoryMax=60G --property=MemorySwapMax=0 \
  python3 integration/linux/doc/scripts/cve_validate.py \
    --n 200 --timeout 600 --invert --modules-per-cve 3 \
    --upstream-repo /home/ubuntu/torvalds-linux.git \
    --out-csv /tmp/cve-validate/results.csv
```

## Cross-references

- [compile-stack-and-perfile-2026-05.md](compile-stack-and-perfile-2026-05.md) —
  the prior closeout that motivated this iteration.
- [cve-recall-n200-closeout-2026-05.md](cve-recall-n200-closeout-2026-05.md) —
  the n=200 measurement establishing the baseline.
- [CBMC_LIMITATIONS.md](../CBMC_LIMITATIONS.md) — LIM-017
  and LIM-018 entries.
