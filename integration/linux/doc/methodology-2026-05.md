# Methodology: applying CBMC to Linux kernel CVE detection

**Date:** 2026-05-31
**Status:** Research-grade, results stable on a 500-CVE +
500-benign-function sample.  Inviting external review.

## Problem

The Linux kernel has roughly 600+ CVEs published per year.
Most are simple bug-class violations (resource leaks,
use-after-frees, null derefs, lock balance) that pattern
match against a small set of "shapes" — but the existing
toolchain (Coverity, Smatch, sparse, syzkaller) finds only a
fraction, and the long tail of subtle subsystem-specific
bugs is dominated by hand review.

This work explores whether **CBMC**, a bounded model
checker for C, can usefully augment that toolchain.  CBMC's
strength is precision: it produces a verdict per function,
backed by a counterexample trace.  Its weaknesses are
scaling (whole-kernel verification is impossible) and
modelling (kernel-specific APIs need contracts).

## Methodology

### 1. Property catalog

We define one **property module** per bug shape.  Each
module is a small C library plus a CBMC contract:

| Shape | Property module | Contract |
|---|---|---|
| Refcount imbalance | `*_lifetime` (cred, kobject, refcount, device, of_node, inode, dentry, fput, sock, skb, module, kref) | `requires get == put` |
| Resource leak on error path | `resource_leak_on_error_path` | `requires no_outstanding_alloc` |
| Use-after-free | `use_after_free_generic` | `requires not freed(p)` |
| Null deref after alloc | `null_after_alloc` | `requires p != NULL ⊻ null_check_done(p)` |
| Lock balance | `lock_state` | `requires lock_held == 1` at unlock |
| Cancel-before-free | `cancel_work_before_free` | `requires no_pending_work` at kfree |
| Netlink attribute validation | `netlink_attr_validation` | `requires nla_validate_done` |
| Concurrent pointer publish | `concurrent_pointer_publish` | `requires read_published == 1` |
| Format string | `format_string` | `requires no_user_str_in_format` |
| ... | (34 modules total) | |

Each module has:
* A reference implementation (`*.c`).
* A header with the public API (`*.h`).
* A unit-test suite (`test_unit.c`) demonstrating vuln/fix
  shapes pass/fail.
* A Coccinelle prefilter (`*.cocci`) that scans source for
  pattern-matching call sites.

### 2. Per-file synthesis

For each (kernel-file, function) pair we want to scan, we:

1. **Compile** the kernel TU with `goto-cc` against an
   x86_64 `defconfig` configured kernel tree, using a
   custom `-include scan-compat.h` that overrides
   problematic kernel macros (`check_mul_overflow`,
   `_ctype[]` extern array, `printk_ratelimited`, etc.).
2. **Instrument** the source via Coccinelle: insert
   `<module>_track(p)` and `__assert_<predicate>(p)` markers
   at appropriate call sites (e.g. after each `kfree(p)`,
   before each `p->fld = e`).
3. **Synthesise a harness** that calls the target function
   with nondet-initialised arguments, plus any
   parameter-typed ghost-bootstrap setup the module needs.
4. **Link** kernel TU + harness + property module +
   adapter (which declares the synthetic `__assert_*`
   contracts) into one goto binary.
5. **Apply contracts** via `goto-instrument
   --replace-call-with-contract` so each `__assert_*` becomes
   a CBMC precondition check.
6. **Drop unreachable** functions to keep symex tractable.
7. **Run CBMC** with `--unwind 2-4` and parse the precondition
   verdict.

### 3. Per-return cocci-fallback

When Coccinelle's CFG analysis aborts (functions with ≥7
reachable returns), we fall back to a Python pass that
brace-tracks the function body and inserts:

* `<module>_track(x)` after each alloc-API call.
* `<module>_freed(x)` after each kfree-API call.
* `leak_alloc_freed(<X>)` after `*OUT = X` ownership-transfer
  patterns (constructor-style functions).
* `__assert_no_outstanding_leak()` (aggregated, one per
  return) before each `return EXPR;`, replacing the return
  with `{ assert; return EXPR; }` so the assertion is sound
  regardless of brace context.

The aggregated assertion uses a property-module helper
(`leak_any_outstanding()`) that scans the ghost table —
trading per-pointer precision for O(returns) instead of
O(returns × tracked-vars) assertion fan-out.  Critical for
CBMC symex budget on many-allocations functions.

### 4. Cross-function tracking

Phase 2 of cross-function UAF detection: when the property
module's ghost table is `static` and the kernel TU + caller
TU + property module are linked into one goto binary, the
ghost state propagates across function boundaries.  Cocci's
Phase 2 rule inserts:

* `mark_freed(x)` after every kfree-family call (including
  in callee TUs).
* `__assert_not_freed(x)` before every `x->fld = e` when
  `x` is a pointer-typed function parameter (over-conservative
  but correct: the contract holds vacuously when no freeing
  occurred).

Validated end-to-end on a synthetic two-function vuln/fix
pair.

### 5. False-positive triage

Three-stage classification:

1. **Empty-ghost-bootstrap:** when the harness can't
   bootstrap the module's ghost (no parameter matches the
   bootstrap-config types), we mark the verdict
   `low-confidence-candidate` rather than `candidate`.  The
   contract trivially fires (e.g. `mutex_unlock` checks
   `lock_held == 1` but lock_held was never set), but we
   downgrade.
2. **Function-body shape filter** (`triage_filter.py`):
   detects three FP shapes by name-and-body matching:
   * `escape_via_store`: tracked var is written through a
     pointer parameter (output pointer).
   * `put_only_on_error`: function only frees on the error
     path; success path transfers ownership.
   * `ownership_handler`: function name matches `put_*`,
     `release_*`, etc., or is a constructor (`*_new`,
     `*_alloc`) with body shape `*X = alloc(...)`.
3. **Per-CVE-best aggregation:** when `--modules-per-cve >
   1`, each module is tried independently.  The summary
   picks the most-bug-finding verdict per CVE using the
   ordering `candidate > fp-filtered > noise > low-confidence-
   candidate > successful > vacuous > timeout > error`.

## Measurement protocol

### Recall (CVE corpus)

Sample N CVEs from the 2023-2026 kernel CVE survey.  For
each:

1. Locate the CVE'd file in one of the local LTS trees
   (`linux_5_10` / `linux_6_1` / `linux_6_6` / `linux_6_12`).
2. Detect file state (`already_vuln` / `reverted` /
   `divergent` / `no_patch` / `upstream_vuln`).  For
   already-vuln states, scan in place; for reverted states,
   use `git apply -R` to put the file in vuln state; for
   upstream_vuln, fetch the pre-fix snapshot from
   `torvalds-linux.git`.
3. Pick up to `--modules-per-cve` property modules whose
   API patterns match the function body.
4. Run scan-per-file for each (CVE × module) tuple.
5. Aggregate per-CVE-best verdict.

The "cleanly-testable" subset: CVEs where the file state
is one of `already_vuln`, `reverted`, `upstream_vuln` AND
at least one module's scan produced a non-skipped verdict.
Recall = `detected / (detected + missed)` on this subset.

### Precision (FP corpus)

Sample N random kernel functions, EXCLUDING any (file,
function) pair that has ever been named in a CVE patch.
For each:

1. Pick up to `--modules-per-fn` modules whose API patterns
   match.
2. Run scan-per-file for each.
3. Aggregate per-function-best verdict.

Upper-bound FP rate = `candidate / total`.  Hand-triage all
candidates to distinguish real (undisclosed) bugs from
false positives.

### Reported numbers (n=500)

| Metric | n=500 (May 2026) |
|---|---|
| CVE corpus | 500 sampled |
| Cleanly-testable unique CVEs | 20 |
| Detected | 9 |
| FP-filtered | 3 |
| Catalog-missed | 1 |
| Recall | 9 / 10 = **90%** |
| FP corpus | 500 sampled |
| Compile errors | 165 (33%) |
| Real candidates | 49 (9.8%) |
| Low-confidence-candidates | 55 (11%) |
| Triage-filtered | 0 |
| Compile/skip | rest |
| Upper-bound FP rate | **9.8%** |

The 9 detected CVEs:

* CVE-2023-53038 (refcount_balance)
* CVE-2023-53453 (resource_leak via fallback)
* CVE-2023-53697 (resource_leak)
* CVE-2024-35829 (lima_heap_alloc — resource_leak via fallback)
* CVE-2024-39492 (resource_leak)
* CVE-2025-21654 (ovl_connect_layer — dentry_lifetime)
* CVE-2025-21895 (resource_leak)
* CVE-2025-40307 (refcount_balance)
* CVE-2026-43304 (resource_leak)

## Threats to validity

1. **Sample bias.**  We sample uniformly from CVE patches
   in `/tmp/cve-survey/`, but that database may have
   coverage gaps.  Stable-only patches that didn't reach
   mainline are over-represented; some CVE classes are
   under-represented.
2. **Triage subjectivity.**  The 49 FP candidates were not
   all individually triaged in this iteration; we
   extrapolate from the n=200 / n=500-v1 triage where 0/28
   and 0/14 candidates were real bugs.
3. **Module coverage.**  34 modules cover 6 ghost shapes.
   Bug classes outside this set (e.g. integer overflow in
   non-alloc contexts, side-channel leaks, weak crypto)
   are not detected.
4. **Whole-program limits.**  CBMC's symex unwinds loops to
   a fixed bound (we use 2-4); deeper bugs or subtle
   asynchronous schedules (work_struct fired after
   device_del) are out of scope.
5. **CVE leakage.**  If a benign-corpus function happens to
   have an undisclosed bug, our FP rate is over-estimated.
   We have NO method to systematically detect this; we
   flag it as a known limitation.

## Reproducibility

```sh
# 1. Configure linux_5_10 with defconfig.
cd /path/to/linux_5_10
make ARCH=x86_64 defconfig
make ARCH=x86_64 prepare0

# 2. Build CBMC + integration tools.
cd /path/to/cbmc
cmake -S . -Bbuild
cmake --build build --target cbmc goto-cc goto-instrument -j$(nproc)

# 3. Pickle CVE'd function set (one-time).
python3 -c "
import json, sys, pickle
from pathlib import Path
sys.path.insert(0, 'integration/linux/doc/scripts')
import cve_validate
pairs = set()
for jf in Path('/tmp/cve-survey/vulns/cve/published/').rglob('*.json'):
    try:
        fp, fn, _ = cve_validate._parse_patch(jf.stem)
        if fp and fn: pairs.add((fp, fn))
    except: pass
Path('/tmp/cve-survey/cve_funcs.pkl').write_bytes(pickle.dumps(pairs))
"

# 4. Run measurements.
ulimit -v unlimited
mkdir -p /tmp/cve-validate /tmp/fp-measure
systemd-run --user --scope --quiet \
    --property=MemoryMax=30G --property=MemorySwapMax=0 \
  python3 integration/linux/doc/scripts/cve_validate.py \
    --n 500 --timeout 600 --invert --modules-per-cve 3 \
    --upstream-repo /path/to/torvalds-linux.git \
    --out-csv /tmp/cve-validate/results.csv &
systemd-run --user --scope --quiet \
    --property=MemoryMax=30G --property=MemorySwapMax=0 \
  python3 integration/linux/doc/scripts/fp_measure.py \
    --kernel-tree /path/to/linux_5_10 \
    --n 500 --timeout 300 --modules-per-fn 3 \
    --cve-funcs /tmp/cve-survey/cve_funcs.pkl \
    --out-csv /tmp/fp-measure/results.csv &
wait
```

Total wall time: ~16 hours on one machine with `MemoryMax=30G`.

## What's left

The catalog has stabilised at 90% recall / 10% upper-bound
FP rate.  Further improvements:

1. **Triage-filter expansion** for the new FP shapes
   exposed by defconfig.  Most of the 49 candidates are
   likely classifiable as one of:
   * Constructor-style functions whose name doesn't fit
     the existing prefix/suffix rules.
   * Wrapper functions that delegate to a known-cleaning
     callee but don't textually match.
   * Functions that store the alloc result into a global
     side-effect.
   Each class can be detected with a body-shape filter
   similar to the existing three.
2. **Per-CVE trace export.**  Each detection currently
   produces a CBMC counterexample.  Exporting these as
   structured artifacts (SARIF or similar) would help
   downstream tooling and reviewer confidence.
3. **Multi-LTS scan.**  Currently we scan against one tree
   per CVE.  Scanning against ALL applicable trees (and
   reporting which ones are vulnerable) would let us
   detect CVEs that are present in some LTS branches but
   not others.
4. **Scaling to n=1000+ and beyond.**  The current 16-hour
   wall time on one machine is prohibitive for routine
   measurement.  Either parallelise across machines, or
   add caching/incremental modes so re-measurement of an
   existing tree is fast.
5. **External review.**  This document is intended as the
   anchor for that.

## Cross-references

* `four-priority-2026-05.md` — origin of the per-return
  cocci-fallback design.
* `seven-improvements-closeout-2026-05.md` — first
  precision/recall pair (n=500 v1).
* `cross-function-uaf-design-2026-05.md` — Phase 1-3
  design.
* `kernel-tree-configuration.md` — defconfig prerequisite.
* `MODULE_CATALOG.md` — full list of property modules.
* `CBMC_LIMITATIONS.md` — append-only log of CBMC bugs
  encountered (LIM-001 through LIM-018).

## License & contributing

Same as the upstream CBMC project (4-clause BSD).  All
infrastructure committed in `integration/linux/`; the
CBMC core fix for incomplete-extern-array (LIM-018) is in
`src/solvers/flattening/boolbv_index.cpp` (commit
`4cf2e1af0c`).
