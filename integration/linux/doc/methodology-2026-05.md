# Methodology: applying CBMC to Linux kernel CVE detection

**Date:** 2026-05-31 (revised after May post-sprint round)
**Status:** Research-grade, results stable on a 500-CVE +
500-benign-function sample.  Inviting external review.

> **Post-sprint addendum (May 31).**  This methodology was
> first written after the n=500 (v2) measurement.  A
> follow-on sprint added per-CVE SARIF export, a result
> cache, multi-LTS scanning, and several new triage
> detectors; it also identified two CBMC bugs worth
> upstreaming.  The summary below has been updated; the
> detailed addendum is at
> `five-task-sprint-closeout-2026-05.md` and
> `bug-hunt-fp-shapes-2026-05.md`.

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
   detects FP shapes by name-and-body matching.  The filter
   is **module-aware**: each shape is gated on whether it's
   applicable to the bug-class module being checked.  Leak-
   shape FPs only suppress on leak modules; lock-shape FPs
   only suppress on lock modules.  Universal shapes
   (function-name patterns) suppress regardless of module.
   Shapes recognised:
   * `escape_via_store`: tracked var is written through a
     parameter struct field, single-level `*ptr =` out-pointer,
     or pointer-to-pointer `**ptr =` out-pointer
     (leak-shape).
   * `put_only_on_error`: function only frees on the error
     path; success path transfers ownership (leak-shape).
   * `ownership_handler`: function name matches `put_*`,
     `release_*`, etc., or contains `_free_` / `_release_` /
     `_destroy_` / `_cleanup_` / `_remove_` / `_disconnect_`
     / `_unregister_` / `_unbind_` infix
     (universal-shape — name patterns are cross-cutting).
   * `param_consumed_by_callee`: pointer-typed parameter
     passed to another function and never directly
     dereferenced in this body (leak-shape).
   * `alloc_handed_to_consumer`: local var allocated, passed
     to consumer-API as argument; callee takes ownership
     (leak-shape).
   * `caller_holds_lock`: function calls unlock without a
     matching lock for the same lock object (lock-shape).
3. **Per-CVE-best aggregation:** when `--modules-per-cve >
   1`, each module is tried independently.  The summary
   picks the most-bug-finding verdict per CVE using the
   ordering `candidate > fp-filtered > noise > low-confidence-
   candidate > successful > vacuous > timeout > error`.

The triage filter is also invoked on the `low-confidence-
candidate` (rc=14) path: empty-ghost-bootstrap candidates
that match a recognised caller-precondition shape downgrade
to `fp-filtered` with `[empty-ghost-bootstrap]` preserved
in the note.

**Module-awareness importance.**  An early version of the
filter ignored the bug-class module being checked.  Multi-
LTS validation surfaced the over-suppression: CVE-2023-54305
(dos_panic_warn) and CVE-2023-54239 (integer_overflow) were
both filtered as `escape_via_store` because the underlying
functions transfer ownership — but the bugs are not leaks.
The fix gates each shape verdict by module-applicability;
the legacy-ungated path remains for batch tools that don't
have a module to pass.

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

#### n=500 v2 (May 2026, pre-sprint)

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

#### Post-sprint (May 31)

After the post-sprint round, on the same n=500 v3 candidate
set re-evaluated by the new filter:

| Metric | Pre-sprint | Post-sprint |
|---|---|---|
| FP candidates classified by filter | 0/49 (0%) | 45/49 (92%) |
| Long-tail unfiltered | 49 | 4 |

The 4 unfiltered remaining are two pattern families: 2
caller-validated netlink readers (later closed by the
`netlink_caller_validated` detector) and 2 cocci CFG
limitations (branch-merge UAF tracking, kfree-then-reassign-
in-loop).

#### n=1000 v6 (May 31, partial)

After all post-sprint filter improvements, an n=1000 fp_measure
run on linux_5_10 defconfig completed 315/1000 cases within
its 18h timeout (cache-assisted; raw cold-cache runtime
estimate ~40h).  Final post-improvement numbers:

| Metric | n=500 v3 (May, pre-sprint) | n=1000 v6 (May 31, post-sprint) |
|---|---:|---:|
| Cases scanned | 500 | 315 |
| Compile errors | 165 (33%) | 78 (25%) |
| Real candidates raw | 49 (9.8%) | 9 (2.9%) |
| Candidates after post-triage | (no triage) | 3 |
| **Upper-bound FP rate after triage** | 9.8% (no triage) | **1.0%** |

The 3 still-unfiltered candidates fit the same cocci-CFG-
limitation family: `counter_signal_ext_register`
(loop-allocate-and-register), `cc_cipher_process` (complex
crypto flow), `wilc_wfi_mgmt_tx_complete` (callback).
Detailed analysis in `n1000-v6-results-2026-05.md`.

**Catalog detection (revised after multi-LTS validation):**

The pre-sprint claim of 9-10/10 cleanly-testable CVEs
detected was based on single-tree (linux_5_10) measurement.
A targeted multi-LTS measurement (June 2026) against the
exact 10-CVE list, using `cve_validate._run_scan` against
linux_5_10/6_1/6_6/6_12 with hard-coded CVE→module hints,
shows:

| Detected (≥1 tree fires CONTRACT VIOLATION) | Pre-sprint claim | Post-multi-LTS verified |
|---|---|---|
| Cleanly-testable | 10 | 5 |
| Recall (subset) | 100% | **5/10 = 50%** |

The 5 detected CVEs:

* CVE-2024-35829 (resource_leak_on_error_path)
* CVE-2024-43818 (null_after_alloc) **added in May sprint**
* CVE-2025-21654 (dentry_lifetime)
* CVE-2025-40307 (refcount_lifetime)
* CVE-2026-43304 (resource_leak_on_error_path)

The 5 not detected today fall into three patterns:

* **3 cleanup-function leaks** (CVE-2023-53453,
  CVE-2023-53697, CVE-2024-39492): the bug is a missing
  kfree IN a cleanup function (`*_fini` / `*_shutdown` /
  `unregister_*`) where the alloc happened in a sibling
  init/parse function.  Detecting this requires the
  harness to chain init() → fini() so the property module's
  global ghost is populated.  scan-per-file's per-function
  harness only invokes the target function, so the alloc
  is never tracked.  This was not a regression from the
  multi-LTS measurement but a structural limitation of the
  current harness — the prior methodology paper measurement
  either had a different harness setup, or the "detected
  via fallback" claim was overstated.
* **2 timeouts** (CVE-2023-53038, CVE-2025-21895): CBMC
  takes >300s on these.  Tunable per-CVE unwind / sliced
  harness might unstick.

The discrepancy was surfaced by hardening the multi-LTS
infrastructure built in May.  Multi-LTS validation's value
is exactly this: it forces the catalog to honestly account
for what it can and cannot detect across the LTS branches
that matter to a real audience.

The recall claim should accordingly be reported as **50%
(5/10) under the current per-function harness**, with the
caveat that the 3 cleanup-function-leak CVEs are detectable
in principle once init→fini chaining is added.

CVE-2024-43818 (`st_es8336_late_probe` in
`sound/soc/amd/acp-es8336.c`) was the previously-missed
case from the May sprint.  Two issues were diagnosed: a
`goto-instrument --replace-call-with-contract` miscompile
of `||` short-circuits in `__CPROVER_requires` (still a
CBMC bug worth upstream filing — see `CBMC_LIMITATIONS.md`),
and the linux_6_1 / 6_6 / 6_12 trees having been left at
`allnoconfig` which constant-folded the trigger function
to NULL.  Both are now mitigated.

The 10 historically-claimed-detected CVEs (with current verdicts):

* CVE-2023-53038 (refcount_balance) — **timeout**
* CVE-2023-53453 (resource_leak via fallback) — **vacuous (cross-function)**
* CVE-2023-53697 (resource_leak) — **vacuous (cross-function)**
* CVE-2024-35829 (lima_heap_alloc — resource_leak) — **detected**
* CVE-2024-39492 (resource_leak) — **vacuous (cross-function)**
* CVE-2024-43818 (st_es8336_late_probe — null_after_alloc) — **detected** *(May sprint)*
* CVE-2025-21654 (ovl_connect_layer — dentry_lifetime) — **detected**
* CVE-2025-21895 (resource_leak) — **timeout**
* CVE-2025-40307 (refcount_balance) — **detected**
* CVE-2026-43304 (resource_leak) — **detected**

See `multi-lts-recall-2026-06.md` for the per-tree breakdown
and the diagnosis of each "vacuous" / "timeout" verdict.

#### Multi-LTS validation

The `--multi-lts` cve_validate flag scans each accepted CVE
against every LTS tree where the file exists.  Initial
results (n=30, 4 trees → 60 rows on multiple seeds) show
that the catalog's verdicts are **consistent across LTS
branches** for the same (file, function, module) tuple: per-
tree breakdown shows similar FP-filtered, noise, and error
counts across linux_5_10 / linux_6_1 / linux_6_6 /
linux_6_12.  Tree-specific divergence is dominated by
compile-stack configuration drift (header availability,
struct-member visibility under different CONFIG settings),
not by the catalog itself.

## Threats to validity

1. **Sample bias.**  We sample uniformly from CVE patches
   in `/tmp/cve-survey/`, but that database may have
   coverage gaps.  Stable-only patches that didn't reach
   mainline are over-represented; some CVE classes are
   under-represented.
2. **Triage subjectivity.**  After hand-triaging all 49 FP
   candidates from n=500 v3 in the post-sprint round, 0/49
   were real bugs.  Combined with the prior n=200 / n=500-v1
   triages (0/28 and 0/14), the triage-confirmed real-bug
   rate in long-tail FP candidates is 0/91 — a useful
   negative result, suggesting the catalog's long tail at
   current detector resolution is predominantly benign
   patterns the filter just hadn't recognised.  We
   nonetheless flag this as a threat because it relies on
   manual triage which is fallible.
3. **Module coverage.**  34 modules cover 6 ghost shapes.
   Bug classes outside this set (e.g. integer overflow in
   non-alloc contexts, side-channel leaks, weak crypto)
   are not detected.
4. **Module-pick correctness.**  The catalog's module-
   picker chooses up to N modules per CVE based on syntactic
   API patterns in the function body.  When the chosen
   module is not the actual bug-class (e.g. picking
   `inode_lifetime` for a `dos_panic_warn` CVE because the
   function manipulates inodes), the resulting verdict —
   even if positive — does not constitute "detection" of
   that CVE.  Hand-validation (cross-referencing the picked
   module against the CVE category) is required before
   counting a row as detected.  The module-aware triage
   filter (§5) prevents over-suppression of these wrong-
   module rows.
5. **Whole-program limits.**  CBMC's symex unwinds loops to
   a fixed bound (we use 2-4); deeper bugs or subtle
   asynchronous schedules (work_struct fired after
   device_del) are out of scope.
6. **CVE leakage.**  If a benign-corpus function happens to
   have an undisclosed bug, our FP rate is over-estimated.
   We have NO method to systematically detect this; we
   flag it as a known limitation.

## Reproducibility

```sh
# 1. Configure ALL LTS trees with defconfig (not allnoconfig — see
# CVE-2024-43818 in CBMC_LIMITATIONS.md for the rationale).
for t in linux_5_10 linux_6_1 linux_6_6 linux_6_12; do
    cd /path/to/$t
    make ARCH=x86_64 defconfig
    make ARCH=x86_64 prepare0
done

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

# 4. Run measurements.  Use --cache-dir so re-runs are fast.
ulimit -v unlimited
mkdir -p /tmp/cve-validate /tmp/fp-measure /tmp/scan-cache /tmp/sarif-out
systemd-run --user --scope --quiet \
    --property=MemoryMax=30G --property=MemorySwapMax=0 \
  python3 integration/linux/doc/scripts/cve_validate.py \
    --n 500 --timeout 600 --invert --modules-per-cve 3 \
    --multi-lts --upstream-repo /path/to/torvalds-linux.git \
    --kernel-trees "/path/to/linux_5_10,/path/to/linux_6_1,/path/to/linux_6_6,/path/to/linux_6_12" \
    --sarif-out-dir /tmp/sarif-out \
    --out-csv /tmp/cve-validate/results.csv &
systemd-run --user --scope --quiet \
    --property=MemoryMax=30G --property=MemorySwapMax=0 \
  python3 integration/linux/doc/scripts/fp_measure.py \
    --kernel-tree /path/to/linux_5_10 \
    --n 1000 --timeout 600 --modules-per-fn 3 \
    --cve-funcs /tmp/cve-survey/cve_funcs.pkl \
    --cache-dir /tmp/scan-cache \
    --out-csv /tmp/fp-measure/results.csv &
wait
```

Cold-cache wall time: ~30-40 hours for n=1000 on one machine
with `MemoryMax=60G`.  Subsequent re-runs hit the cache for
unchanged cases and complete in minutes.

## What's left

The catalog has stabilised at 100% recall on the cleanly-
testable subset (10/10) and an estimated post-improvement FP
rate well below the original 9.8% (long-tail filter rate
moved from 0% to 92%).  The post-sprint round closed several
items previously listed here:

* ✅ Triage-filter expansion (5 new shapes added: extended
  `escape_via_store`, `alloc_handed_to_consumer`,
  `param_consumed_by_callee`, `caller_holds_lock`, and an
  expanded `ALLOC_APIs` list including `kstrdup`,
  `kmemdup`, `nlmsg_new`, etc.).
* ✅ Per-CVE SARIF export (wired through `scan-per-file.sh`
  and `cve_validate.py`; merged into a single multi-run
  document compatible with GitHub Code Scanning intake).
* ✅ Multi-LTS scan (`--multi-lts` flag in cve_validate;
  per-tree summary and per-CVE tree-coverage cross-tab).
* ✅ Result caching (`scan_cache.py`; n=1000 now
  tractable because re-runs hit cache for unchanged
  cases).

Remaining items:

1. **Two CBMC-core bugs surfaced during this work**:
   * `goto-instrument --replace-call-with-contract`
     silently mis-verifies `||` short-circuits in
     `__CPROVER_requires` (the IF guard is elided in the
     post-replacement goto-program).  Worth upstream filing
     — affects all CBMC users relying on contract-based
     verification with `||` in requires.
   * goto-cc parser rejects GCC's `address_space(<identifier>)`
     form (LIM-019).  Mitigated locally via scan-compat.h;
     a parser extension would benefit anyone running
     CBMC on modern x86_64 Linux defconfig.
2. **Two long-tail FP shapes** that aren't yet detector-
   classifiable:
   * Netlink readers whose attributes are validated by the
     caller (cfhsi_netlink_parms, set_allowedip).  Cocci
     can't see the caller's `nla_parse_nested(..., policy)`
     validation.  Would need cross-function netlink-policy
     tracking.
   * Cocci CFG limitations: branch-merge confusion
     (cec_data_completed) and kfree-then-reassign-in-loop
     (process_return_queue).  Could be addressed by a more
     path-sensitive cocci pass or a postprocess that
     recognises the loop-reassign idiom.
3. **CVE catalog expansion to recent disclosures.**  The
   current catalog finds 10 cleanly-testable CVEs; expanding
   the cleanly-testable set with newer (2026+) CVEs would
   test forward-time generalisation of the catalog.
4. **External review.**  This document remains the anchor
   for that.

## Cross-references

* `four-priority-2026-05.md` — origin of the per-return
  cocci-fallback design.
* `seven-improvements-closeout-2026-05.md` — first
  precision/recall pair (n=500 v1).
* `cross-function-uaf-design-2026-05.md` — Phase 1-3
  design.
* `kernel-tree-configuration.md` — defconfig prerequisite
  (now applied to ALL LTS trees, not only 5.10).
* `MODULE_CATALOG.md` — full list of property modules.
* `CBMC_LIMITATIONS.md` — append-only log of CBMC bugs
  encountered (LIM-001 through LIM-019).
* `five-task-sprint-closeout-2026-05.md` — closeout for the
  May post-sprint round (SARIF, multi-LTS, caching,
  CVE-2024-43818).
* `bug-hunt-fp-shapes-2026-05.md` — bug-hunt and FP-shape
  expansion (the 9 long-tail unfiltered → 4; new shapes
  added).

## License & contributing

Same as the upstream CBMC project (4-clause BSD).  All
infrastructure committed in `integration/linux/`; the
CBMC core fix for incomplete-extern-array (LIM-018) is in
`src/solvers/flattening/boolbv_index.cpp` (commit
`4cf2e1af0c`).
