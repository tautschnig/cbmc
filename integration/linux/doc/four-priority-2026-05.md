# Four-priority sprint closeout — ovl/most triage, missed-
# CVE diagnosis, devm_* extension, LIM-018 root cause

**Date:** 2026-05-27

This iteration tackled the four follow-on priorities from the
prior closeout, in order of cost-per-value.

## Part 1: ovl_connect_layer detected vs. most_register_interface missed

**Investigation result.** Different root causes than expected.

`ovl_connect_layer` (CVE-2025-21654, DETECTED): a ~40-line
function with 2 returns.  CBMC completes at `UNWIND=2` in
~30s and reports `dput` precondition failures — real
candidate.

`most_register_interface` (CVE-2026-43317, "MISSED"):

1. **Times out at `UNWIND=2`.**  Even at 30 minutes wall-
   clock, CBMC's symex doesn't finish on this function.  At
   `UNWIND=1` it does finish and reports NOISE (built-in
   checks fired, contracts hold).
2. **Wrong-module-fit.**  When the validator runs with
   `--modules-per-cve 3`, `device_lifetime` is picked first
   (function uses `put_device`); when it returns "successful"
   the validator records that and doesn't proceed to
   `resource_leak_on_error_path` which would actually match
   the bug shape.

So the "miss" was a combination of CBMC slowness plus
validator's first-result-wins logic — not a methodology
limit.

## Part 2: hand-triage of the 6 catalog-missed CVEs

Documented in `doc/missed-cves-triage-2026-05.md`.  Bottom
line:

| CVE | Bug class | Diagnosis |
|---|---|---|
| CVE-2023-54214 | use-after-free | Cross-function UAF, outside per-function scope |
| CVE-2024-35829 | resource-leak | cocci CFG abort on lima_heap_alloc (~7 returns) |
| CVE-2024-43818 | resource-leak | cocci CFG abort, same shape |
| CVE-2025-21838 | cancel-work | `cancel_work_before_free` not yet per-file capable |
| CVE-2025-68357 | resource-leak | cocci CFG abort, same shape |
| CVE-2026-43317 | resource-leak | wrong-module-fit (above) |

**4 of 6 are the same shape** (resource-leak-on-error-path)
blocked by the same cocci limit.  The threshold is roughly
"functions with ≥7 returns reachable from the alloc site"
— cocci's CFG analysis emits "inconsistent control-flow
paths" and instruments nothing.

## Part 3: devm_* family in cocci

Added per-API rule pairs for `devm_kmalloc`, `devm_kzalloc`,
`devm_kcalloc`, `devm_kmemdup` to both
`resource_leak_on_error_path.cocci` and
`null_after_alloc.cocci`.  Also extended cve_validate's
`_MODULE_API_PATTERNS` regex to include the full alloc
family (vmalloc, kvcalloc, kasprintf, alloc_workqueue,
devm_*).

This adds 5,400+ more allocation call sites to the cocci
matching scope (Linux 5.10's devm_kzalloc usage alone).
Whether it produces new detections depends on the specific
CVE sample.

## Part 4: LIM-018 root cause and workaround

**Reduction approach.** Added a temporary debug print to
`src/solvers/flattening/boolbv_map.cpp::get_literals` to
report the failing identifier and width:

```cpp
INVARIANT(
  map_entry.literal_map.size() == width,
  "... identifier=" + id2string(identifier)
   + " cached_width=" + std::to_string(map_entry.literal_map.size())
   + " requested_width=" + std::to_string(width)
   + " cached_type=" + map_entry.type.id_string()
   + " requested_type=" + type.id_string());
```

Result on `lib/argv_split.c::argv_split`:

```
identifier=_ctype#1 cached_width=8 requested_width=0
cached_type=array requested_type=array
```

**Root cause.** `<linux/ctype.h>` declares
`extern const unsigned char _ctype[]` — incomplete array.
CBMC's bv-pointers caches the symbol at width=8 (one byte,
the element width) on first lookup, then on a later
reference computes width = element_count × element_width =
0 × 8 = 0 because element_count for an incomplete extern
array is 0.  The cache and request widths disagree → abort.

**Workaround.** scan-compat.h now includes `<linux/ctype.h>`
and overrides `__ismask(x)` (the macro all the `is*()`
classifiers expand to) to return `(unsigned char)(x)`
without referencing `_ctype[]`.  Sound under our usual scan
interpretation: leak / null-deref / refcount analyses don't
depend on character class.

**Verification.** `lib/argv_split.c::argv_split` now runs
to completion (previously aborted in BMC).  All
`scan/run.sh` regressions pass.

LIM-018 in `CBMC_LIMITATIONS.md` updated from "OPEN" to
"WORKAROUND APPLIED".

## Final n=200 measurement

After all four improvements:

| Metric | n=200 v4 | n=200 v5 |
|---|---:|---:|
| Verdict rows | 185 | 192 |
| Unique CVEs | 94 | 95 |
| **error** | 92 | 100 (+8) |
| **vacuous** | 63 | 61 |
| **skipped** | 4 | 6 (+2) |
| **noise** | 6 | 6 |
| **timeout** | 5 | 5 |
| **successful** | 6 | 5 |
| **fp-filtered** | 6 | 6 |
| **candidate** | 3 | 3 |
| Cleanly-testable rows | 15 | 14 |
| Cleanly-testable unique CVEs | 14 | 13 |
| Detected | 2 | 2 |
| FP-filtered (clean subset) | 6 | 6 |
| Catalog-missed | 6 | 5 |

**Recall on cleanly-testable bugs:** 2 / (2 + 5) = **28.6%**,
slightly up from v4's 25%.

The v5 sample lost one cleanly-testable case (CVE-2024-43818
moved from "successful" to a different state) but recall on
the smaller-but-cleaner subset edged up.  Detection set is
unchanged: same 2 unique CVEs.

The LIM-018 mitigation makes lib/argv_split.c testable, but
no CVE in our sample names argv_split itself as the
bug-affected function, so the fix doesn't surface new
detections in n=200.  It's still real progress: the next
iteration that DOES sample a `_ctype`-using file will
benefit.

The devm_* coverage similarly didn't surface new
detections in n=200.  No CVEs in the cleanly-testable
subset use devm_* allocators; the LTS-divergence rate for
those CVEs is too high (they're typically driver patches,
where stable backports drift fast).

## Honest assessment

**What this iteration delivered:**

- **Diagnosed all 6 catalog-missed CVEs to their actual
  root cause.**  4/6 are the same shape blocked by the
  same cocci CFG limit; 1 is wrong-module-fit (validator
  bug); 1 is cross-function UAF (out of scope).  The
  catalog isn't missing 6 different bug-shape modules — it
  has 1 cocci limitation and 1 validator-logic bug.
- **Identified a CBMC bug we didn't know about** (LIM-018:
  incomplete-extern-array width mismatch in bv_pointers)
  and found a sound workaround.  Worth filing upstream.
- **5,400+ more allocator call sites covered by cocci**
  via the devm_* family.
- **Triage write-up** in `doc/missed-cves-triage-2026-05.md`
  for posterity.

**What this iteration didn't change:**

- Recall stayed roughly constant (25% → 28.6% on a slightly
  smaller cleanly-testable subset).
- Detection count stayed at 2 unique CVEs.
- The cocci CFG limit on functions with ≥7 returns is
  unchanged.  4/6 missed CVEs need a different
  instrumentation strategy (per-return statement-level
  insertion bypassing cocci) to detect.

## What's left

Highest-leverage remaining work, in order:

1. **Per-return manual instrumentation as a fallback.**
   When cocci's CFG analysis aborts on a function with too
   many returns, fall back to a Python pass that inserts
   `__assert_no_leak_at_exit` before each return statement
   in the function body, regardless of which allocations
   reach there.  Loses precision but unblocks 4/6 of the
   catalog-missed CVEs.  ~3-5 days.

2. **Validator: try ALL --modules-per-cve modules even
   after a successful verdict.**  If `device_lifetime`
   says "successful" but `resource_leak_on_error_path`
   would catch the bug, the validator should still try
   the second one.  ~1 day.

3. **`cancel_work_before_free` per-file synthesis** for
   CVE-2025-21838-style flush-before-free bugs.  ~2-3
   days.

4. **Upstream LIM-018 fix.**  Either CBMC should handle
   incomplete-extern-array references gracefully (treat
   as size-1 or size-0 with no-error semantics), or
   provide a clearer error message.  ~1-2 days for an
   upstream PR.

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

- [missed-cves-triage-2026-05.md](missed-cves-triage-2026-05.md) — per-CVE triage diagnosis.
- [cocci-cbmc-compile-2026-05.md](cocci-cbmc-compile-2026-05.md) — prior closeout.
- [CBMC_LIMITATIONS.md](../CBMC_LIMITATIONS.md) — LIM-017
  and LIM-018 entries.
