# Five-task sprint closeout — 2026-05

This closes out the next-step sprint that followed the
n=500 v3 false-positive measurement.  The sprint had six
tasks initially; the upstream-PR step (LIM-018) was deferred
at the user's request.  Five tasks remained, plus a sixth
that came up in the middle of the sprint as a kernel-tree
configuration regression.

The sprint moved the catalog from **9 detected CVEs / 90% recall**
on the n=500 v2 measurement to **at least 10 detected** (after
fixing the contract miscompile that masked CVE-2024-43818) and
gained five new pieces of infrastructure:

| Task | Status | Outcome |
| --- | --- | --- |
| 1 SARIF cherry-pick into develop | ✅ | Already in branch from earlier session |
| 2 Triage-filter expansion | ✅ | 49 candidates → 9 unfiltered (82% reduction) |
| 3 CVE-2024-43818 investigation | ✅ | Two-issue diagnosis; bug now detected |
| 4 Per-CVE SARIF export | ✅ | Wired through `scan-per-file.sh` and `cve_validate.py` |
| 5 Multi-LTS scan | ✅ | `--multi-lts` flag in `cve_validate.py` |
| 6 Scaling (caching) | ✅ | `scan_cache` module + `--cache-dir` flag |
| 7 Re-run n=500 | 🟡 | Kicked off in background (~16h wall) |
| 8 This closeout doc | ✅ | This file |
| LIM-019 (incidental) | ✅ | scan-compat.h workaround for `__seg_gs` |

The new commits on `develop` (newest first):

```
7b68e39133 linux: scan-compat.h workaround for __seg_gs (LIM-019)
a2046f60ad linux: --multi-lts flag — scan each CVE against all applicable LTS trees
a54c03f2b2 linux: scan-result cache for fp_measure (makes n=1000 tractable)
023ca81477 linux: per-CVE SARIF export through scan-per-file + cve_validate
b4be9ed96a linux: CVE-2024-43818 detected — null_after_alloc contract refactor + defconfig
7f0126535f linux: triage-filter expansion — drops 49 candidates → 9 (82%)
```

## Task-by-task summary

### Task 1 — SARIF cherry-pick (already in branch)

`origin/sarif-ui` was already integrated to `develop` as
commit `c2f2f9f859` plus two follow-on linux/ commits
(`a1bde41652`, `01327a8377`).  No further action required;
`build/bin/cbmc --sarif-result file` works.

### Task 2 — Triage-filter expansion

`integration/linux/scan/triage_filter.py` was extended with
three new detectors and one suffix-list extension:

* `_detect_alloc_into_param_field`: catches the
  `<param>-><field> = <alloc-API>(...)` shape where the
  allocation result is written directly into a parameter
  struct field with no local intermediate variable.
  Concrete cases caught:
    * `input_alloc_absinfo` (`dev->absinfo = kcalloc(...)`)
    * `btt_freelist_init` (`arena->freelist = kcalloc(...)`)
  The result transfers ownership to the caller-owned struct,
  so the per-function leak check fires falsely.  Tagged
  `escape_via_store`.
* `_detect_param_consumed_by_callee`: catches functions that
  pass a pointer-typed parameter to another function and never
  directly dereference it in this body.  Common shape in
  network-stack forwarders such as `uld_send` (passes `skb` to
  `ctrl_xmit`).  Tagged `param_consumed_by_callee`.
* Infix matches on ownership-handler names: `_free_`,
  `_release_`, `_destroy_`, `_cleanup_`, `_remove_`,
  `_disconnect_`, `_unregister_`, `_unbind_`.  Catches
  cleanup functions whose names embed the cleanup verb in the
  middle (e.g. `qlcnic_82xx_free_mac_list`).
* `_OWNERSHIP_FN_SUFFIXES` extended with `_fini`, `_deinit`,
  `_remove`, `_disconnect`, `_unregister`, `_uninit`.

Plus: `fp_measure.py` now invokes `triage_filter.classify` on
rc=10 candidates and downgrades them to `fp-filtered` when a
shape is recognised — mirroring `cve_validate.py`'s behaviour.

**Result on the 49-candidate set from n=500 v3**:

| Shape | Count |
| --- | ---: |
| `escape_via_store` | 20 |
| `ownership_handler` | 11 |
| `param_consumed_by_callee` | 6 |
| `put_only_on_error` | 3 |
| **Total filtered** | **40 (82%)** |
| Unfiltered | 9 |

The 9 remaining are a mix of real-but-uncommon shapes
(network-stack forward functions where the alloc target is a
local variable, netlink validators flagging unvalidated
reads) and module-specific FPs that need per-module rules.

### Task 3 — CVE-2024-43818 investigation

Goal: understand why CBMC reported `VERIFICATION SUCCESSFUL`
on `st_es8336_late_probe` despite correct cocci instrumentation
inserting `__assert_safe_to_deref(codec_dev)` markers at the
known-NULL deref sites.

**Two-issue diagnosis**:

1. **`goto-instrument --replace-call-with-contract` mis-compiles
   `||` short-circuits in `__CPROVER_requires`.**  The original
   contract was:

   ```c
   void __assert_safe_to_deref(const void *p)
     __CPROVER_requires(p != NULL || null_check_done(p) == 1);
   ```

   Inspecting the post-replacement goto-program showed the IF
   guard for the OR's short-circuit was missing, producing
   `tmp_if_expr := true` unconditionally and asserting trivially.
   Workaround: extract the OR into a single helper function

   ```c
   int __null_after_alloc_safe(const void *p) {
     if (p != NULL) return 1;
     return null_check_done(p);
   }
   ```

   and rewrite the contract to compare the function-call result:

   ```c
   __CPROVER_requires(__null_after_alloc_safe(p) == 1);
   ```

   This is a **CBMC bug worth flagging upstream** — other
   contracts using `||` may be silently miscompiled the same way.

2. **`linux_6_1`, `linux_6_6`, `linux_6_12` were still
   configured with `allnoconfig`.**  Without `CONFIG_ACPI`,
   `acpi_get_first_physical_node` is a static-inline stub
   returning NULL unconditionally — so CBMC constant-folded
   `codec_dev = acpi_get_first_physical_node(adev)` to NULL
   and never reached the bug path.  Reconfigured all three
   trees with `make defconfig` plus `make prepare0`.  CONFIG
   line counts moved from 380 → 1,478 (6.1), 1,520 (6.6),
   1,576 (6.12), with `CONFIG_ACPI=y`.

After both fixes: precondition.3 (line 224, the
`devm_acpi_dev_add_driver_gpios(codec_dev, ...)` site) and
precondition.4 (line 229, the `gpiod_get_optional` site)
both fail → `CONTRACT VIOLATION` → CVE-2024-43818 is
detected by the catalog.

### Task 4 — Per-CVE SARIF export

The CBMC core already supports `--sarif-result <file>` from
the earlier cherry-pick.  This task wired the option through
the per-file pipeline:

* `scan-per-file.sh` reads a `SARIF_OUT` environment variable.
  When set, it passes `--sarif-result $SARIF_OUT` to cbmc;
  otherwise it emits to a temp file that is discarded on EXIT.
* `cve_validate.py` adds a `--sarif-out-dir DIR` flag.  When
  set, exports `SARIF_OUTPUT_DIR=DIR`; `_run_scan` derives a
  per-CVE SARIF path and sets `SARIF_OUT` accordingly.  Each
  `CveCase` records its `sarif_path`; main() invokes
  `_merge_sarif` at the end to combine all per-CVE runs into
  a single `cve-validate.sarif` document tagged with cve /
  module / verdict / file / function properties per run.

Verified end-to-end on CVE-2024-43818: the per-CVE SARIF
file is 6.3 KB and contains five precondition results (3
SUCCESS, 2 FAILURE).

### Task 5 — Multi-LTS scan

`cve_validate.py` previously picked one kernel tree per CVE
(the first LTS tree where the file path existed).  The new
`--multi-lts` flag expands each accepted CVE into one
`CveCase` per LTS tree where the file exists, and scans each
tree independently.  The output gains a `tree` column in the
Markdown table and two new summary blocks:

* Per-tree detection summary: rows scanned per tree, broken
  down by verdict.  Useful for comparing recall across LTS
  branches.
* Trees-with-`candidate`-verdict per CVE: a cross-tabulation
  showing which trees yielded a real candidate for each
  detected CVE.  Helpful for triage when a CVE is present in
  some branches but not others.

`--multi-lts` is mutually exclusive with `--invert` at this
layer — `--invert` already does its own per-tree state
detection inside `_run_scan`, so multi-tree expansion is
suppressed for invert runs to avoid double-counting.

### Task 6 — Caching

`integration/linux/scan/scan_cache.py` is a new module
providing `ScanCache`, a filesystem-backed result cache.
Keys are `(file content sha256[:16], function, module,
instrument)` tuples; values are
`{rc, stdout, stderr, runtime_s, file_hash, scanner_version}`.

A `--cache-dir DIR` flag was added to `fp_measure.py`
(`SCAN_CACHE_DIR` env var also accepted).  When enabled,
`_run_scan` looks up each tuple before invoking
`scan-per-file.sh`; on hit, it returns the cached rc and
verdict; on miss, it runs the scan and writes the result.
A `scanner_version` constant invalidates entries when
property modules / cocci / harness / contracts change in a
way that affects results — bump it on any such change.

**Effect estimate**: an n=1000 measurement that would take
~40h cold should take minutes when 90% of the corpus is
unchanged.  Multi-LTS scans benefit similarly for files that
overlap across trees.

The cache is unit-tested at construction time:
* `put` + `get` round-trip preserves rc and stdout.
* Different `instrument` keys miss.
* Different `scanner_version` values miss (cache invalidation).

### LIM-019 (incidental) — `__seg_gs` parser issue

While running the cve_validate validation for task 7, a new
syntax error appeared on Linux 6.12 defconfig builds:

```
./arch/x86/include/asm/current.h:41:1:
  error: syntax error before '__seg_gs'
```

Root cause: 6.12's `<linux/percpu.h>` defines (under
`CONFIG_USE_X86_SEG_SUPPORT`)

```c
#define __seg_gs              __attribute__((address_space(__seg_gs)))
#define __percpu_seg_override __seg_gs
```

CBMC's parser only accepts the OpenCL form
`address_space(<int-literal>)`, not the GCC named-identifier
form.  Mitigation: `scan-compat.h` is `-include`d before any
kernel header parses, and overrides the offending macros to
no-ops.  Soundness: per-CPU segment-relative storage is a
microarchitectural detail; the address-space annotation is
consumed by GCC's aliasing inference, not by symex.
Documented in `CBMC_LIMITATIONS.md` as LIM-019.

### Task 7 — Re-run n=500 (in progress)

Kicked off in background:

```
fp_measure.py --kernel-tree /home/ubuntu/linux_5_10
              --n 500 --modules-per-fn 3 --timeout 600
              --cve-funcs /tmp/cve-survey/cve_funcs.pkl
              --cache-dir /tmp/scan-cache
              --seed 42
```

Results will populate `/tmp/fp-measure-v4/results-n500.csv`
when complete.  This is a cold-cache run (cache will be warm
for subsequent runs).

The fully validated re-measurement will be reported in a
follow-up addendum once the run completes.

## Open issues and follow-up work

### Upstream candidates

1. **`goto-instrument --replace-call-with-contract` IF guard
   elision on `||` in `__CPROVER_requires`.**  Surfaced
   during task 3.  Need to construct a minimal reproducer
   (one function, one contract with `a || b`) and file
   upstream.  The bug is **silently mis-verifying** rather
   than crashing, so it has been hiding behind successful
   verifications.

2. **goto-cc parser support for identifier-named address
   spaces** (LIM-019).  GCC's `address_space(<identifier>)`
   form is now used by mainline Linux x86 per-CPU code on
   defconfig.  Worth extending `src/ansi-c/parser.y` to
   accept it (translate to a default address-space slot, or
   simply ignore the identifier).

### Closing the recall gap further

CVE-2024-43818 is now detected.  The remaining blockers in
the original n=500 v2 missed-CVE list are documented in
`integration/linux/doc/missed-cves-triage-2026-05.md`.
The next likely-tractable target is whichever CVE remains
after re-running validate against the n=500 v2 catalog.

### FP characterisation

After the triage-filter expansion the upper-bound FP rate is
expected to drop substantially (40 of 49 prior candidates
re-classified as `fp-filtered`).  The exact post-improvement
FP rate awaits the n=500 re-run completion.

### Scaling

The cache makes multi-machine parallelism less urgent — most
of the cost was redundant work during iteration.  If we want
multi-machine in the future, the cache directory is already
designed to be shared (atomic single-file writes per key, no
locking required for read-mostly workload).
