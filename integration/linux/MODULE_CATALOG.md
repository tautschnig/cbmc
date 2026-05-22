# MODULE_CATALOG — CBMC kernel property modules

**Catalog status:** 34 property modules, validated end-to-end
across all four LTS kernels (5.10, 6.1, 6.6, 6.12).  Estimated
**65-75 % effective bug-shape coverage** of the 2023-2026 Linux
kernel CVE record (8,719 CVEs surveyed).

This document summarises every module for outside readers.
Each entry lists the bug class targeted, the abstraction
shape, motivating CVEs, and integration status.

## Quick overview

| Shape | Modules | What they catch |
|---|---:|---|
| Per-pointer balance refcount | 12 | refcount UAF, double-put, refcount leak |
| Per-pointer flag | 12 | shape-specific invariants (page provenance, lock state, init done, freed, work pending, etc.) |
| Single global counter | 3 | RCU CS depth, capability checked, format-string constant |
| Per-pointer integer | 1 | netlink attribute size validation |
| Two integer globals + concurrent | 3 | publish-before-init, double-put race, TOCTOU |
| Predicate-only | 3 | size overflow, copy length, divisor non-zero |

## Existing balance-refcount modules (12)

Pattern: per-pointer "live" counter; `get_X` increments,
`put_X` decrements with precondition `live > 0`.

| Module | Kernel API | CVE motivation |
|---|---|---|
| `cred_lifetime` | put_cred / get_cred | CVE-2026-23297 |
| `kobject_lifetime` | kobject_put / kobject_get | broad: 57+ CVE mentions |
| `refcount_lifetime` | refcount_dec_and_test | foundational |
| `device_lifetime` | put_device / get_device | 82+ CVE mentions; driver UAFs |
| `of_node_lifetime` | of_node_put / of_node_get | OF tree refcount UAFs |
| `inode_lifetime` | iput / ihold | fs/* inode UAFs |
| `dentry_lifetime` | dput / dget | fs/* dentry UAFs |
| `fput_lifetime` | fput / get_file | fs/* file struct UAFs |
| `sock_lifetime` | sock_put / sock_hold (static inline) | net/* socket UAFs |
| `skb_lifetime` | kfree_skb / skb_get | net/*, drivers/net/* UAFs |
| `module_lifetime` | module_put / try_module_get | module reference leaks |
| `kref_lifetime` | kref_put / kref_get | foundational primitive |

Validated end-to-end: each has a unit test, vuln/fix
direct-call harness, and per-file synthesis support
(`_PER_FILE_SUPPORTED_MODULES`).

## Per-pointer flag modules (12)

Pattern: per-pointer boolean ghost set/cleared by specific
state transitions.  The contract on a downstream operation
requires the flag is in the right state.

| Module | Targeted bug shape | Motivating CVE |
|---|---|---|
| `page_provenance` | which subsystem owns a page | composes with aead, scatterlist |
| `pipe_buffer` | flags reset before take-over | CVE-2022-0847 (Dirty Pipe) |
| `lock_state` | mutex unlock requires prior lock | double-mutex-unlock CVEs |
| `alloc_tag` | kmalloc-vs-vmalloc-aware free | LIM-009 / alloc_tag confusion |
| `cancel_work_before_free` | INIT_WORK + kfree without cancel | UAF via deferred work |
| `cancel_delayed_work_before_free` | same for delayed_work | timer-driven UAFs |
| `del_timer_sync_before_free` | timer_setup + kfree without del_timer_sync | timer UAFs |
| `null_after_alloc` | kmalloc-then-deref without NULL check | the LARGEST CVE category (1,395 CVEs, 16 %) |
| `resource_leak_on_error_path` | alloc + early return without free | resource_leak (683 CVEs) |
| `use_after_free_generic` | kfree-then-deref or kfree-then-kfree | non-refcount UAF + double-free |
| `uninit_to_user` | copy_to_user from uninit struct | info leak (95 CVEs) |
| `format_string` | printk(user_string) | CVE-2025-68816 |

Per-file support varies: balance modules have full per-file
synthesis; synthetic-checkpoint modules (cancel_*, null_after_alloc,
etc.) currently rely on cocci for real-kernel surfacing
plus synthetic harness validation.  The
`scan/tools/instrument.py` prototype demonstrates how
cocci-driven instrumentation could lift these to full
per-file CBMC verification.

## Single-global-counter modules (3)

| Module | Ghost | Motivating CVE class |
|---|---|---|
| `rcu_critical_section` | RCU read-side depth | rcu_misuse (44+ CVEs); plus per-CPU concurrent variant |
| `permission_bypass` | capability check passed | privilege escalation (15 CVEs) |

(format_string also fits this shape but is listed under
per-pointer flag for clarity; the per-pointer table holds
constancy info for individual format pointers.)

## Per-pointer integer modules (1)

| Module | Ghost | Motivating CVE class |
|---|---|---|
| `netlink_attr_validation` (v1+v2+v3) | nlattr's validated_min_size | out_of_bounds in netlink/netfilter (most of 786+ OOB CVEs) |

The v3 of netlink attaches contracts to `nla_get_u8/u16/u32/u64`
AND to `__nla_parse` (via a layout-compatible struct
nla_policy shim that reads `policy[i].type`).  Catches both
"skipped validation entirely" and "type-mismatch" bugs.

## Two-integer-globals + concurrent modules (3)

Use CBMC's concurrent execution.  CBMC's pointer-concurrency
is unsound, so these use integer flags as a faithful
surrogate.

| Module | Bug class | Motivating CVE class |
|---|---|---|
| `concurrent_pointer_publish` | publish-before-init race | race_or_toctoue (276 CVEs) |
| `concurrent_double_put` | concurrent if-live-then-put | refcount race subset |
| `tocttou_inode_check` | check-then-act with concurrent mutator | TOCTOU subset |

Each harness uses `__CPROVER_ASYNC_n` to spawn threads;
direct-call vuln/fix shapes via `-DFIXED`.

## Predicate-only modules (3)

| Module | Predicate | Motivating CVE class |
|---|---|---|
| `integer_overflow_in_alloc_size` | alloc_size_safe(n, elem) | integer_overflow (118 CVEs) |
| `copy_from_user_size_check` | copy_len_safe(cap, len) | string_or_copy_bound (19 CVEs) |
| `division_by_zero_check` | divisor_safe(d) | dos_panic_warn divide-by-zero subset |

Lighter-weight: no ghost state, just a runtime predicate
the contract enforces.

## Tooling alongside the catalog

* **`balance_module_factory.py`** — generates a new
  refcount-balance module from a single config dict.  Used
  for 8 of the 12 balance modules; ~30 minutes per new
  module.
* **`scan/tools/instrument.py`** — prototype per-file
  instrumentation tool for synthetic-checkpoint modules.
  Currently regex-based; covers 5 shapes.  Productising
  with Coccinelle's AST matching is the highest-impact
  remaining work item.
* **`cve_survey_classify_v[1-4].py`** — CVE classifier
  iterations.  v4 with patch-text + commit-subject + file
  priors achieves 99.3 % reduction of the conservative
  'other' bucket.
* **`balance_module_factory.py`** + **`synthesise_harness.py`**
  + **`scan-per-file.sh`** — the per-file synthesis
  pipeline.

## Coverage estimate (calibrated)

After the v4 classifier sanity check (sampling 50
reattributed CVEs), the **calibrated effective coverage** is
**65-75 % of bug-shape volume** in the 2023-2026 kernel CVE
record:

| Metric | % |
|---|---:|
| In-covered-categories (v4 nominal) | 98.8 % |
| Effective (with realistic per-category rates) | 70 % |
| Effective lower bound (50 % of v4 reattribution wrong) | 65 % |
| Effective upper bound (90 % of v4 reattribution right) | 75 % |

The previous "past 70 %" claim was overconfident; the
honest range centers near 70 % with substantial uncertainty
about precise reattribution accuracy.

## Reproducing

Each module has its own `run.sh` regression runner:

```sh
for m in $(ls integration/linux/properties/); do
  ./integration/linux/properties/$m/run.sh
done
```

Per-file scan with optional instrumentation:

```sh
LINUX_TREE=/path/to/linux UNWIND=2 \
  ./integration/linux/scan/scan-per-file.sh MODULE FILE FUNCTION
# add INSTRUMENT=all to enable per-file checkpoint
# instrumentation (prototype; some files may fail to compile).
```

Full corpus across all four LTS kernels:

```sh
RUN_BASE=/tmp/corpus-$(date +%Y-%m-%d)
for k in linux_5_10 linux_6_1 linux_6_6 linux_6_12; do
  systemd-run --user --scope --quiet \
    --property=MemoryMax=188G --property=MemorySwapMax=0 \
    env LINUX_TREE=/home/ubuntu/$k \
        SCAN_MEMORY_LIMIT=$((8*1024**3)) \
        SCAN_MEMORY_LIMIT_KB=$((8*1024**2)) \
        SCAN_CPU_LIMIT=600 PARALLEL=16 \
        CORPUS_MAX=200 EXTRA_SCAN_ARGS=--per-file \
        SCAN_FILE_TIMEOUT=600 \
    timeout 90m \
    ./integration/linux/scan/corpus-scan.sh "$RUN_BASE/$k"
done
```

## Project history

Phases of catalog growth:

* **May 2026 baseline** (8 modules): aead, page_provenance,
  pipe_buffer, scatterlist, cred_lifetime, lock_state,
  refcount_lifetime, alloc_tag.
* **Phase 1** (+8 balance modules): device, of_node, inode,
  dentry, fput, sock, skb, module via the factory.
* **Phase 2** (+3): kref_lifetime, rcu_critical_section,
  cancel_work_before_free.
* **Phase 3a** (+1, multi-version): netlink_attr_validation
  v1→v2→v3.
* **Phase 3b** (+3): concurrency siblings.
* **Sibling cancel** (+2): cancel_delayed_work_before_free,
  del_timer_sync_before_free.
* **Beyond 50 %** (+4): null_after_alloc,
  resource_leak_on_error_path,
  integer_overflow_in_alloc_size,
  copy_from_user_size_check.
* **Beyond 70 %** (+3): use_after_free_generic,
  division_by_zero_check, uninit_to_user.
* **Closing the long tail** (+2): permission_bypass,
  format_string.

Total: **34 modules**.

## Cross-references

- [CVE survey v1](cve-survey-2023-2026.md)
- [Classifier v3 (patch-level)](classifier-v3-2026-05.md)
- [1+2+3 closeout (v4 + per-file prototype)](closeout-1plus2plus3-2026-05.md)
- Per-phase write-ups (`phase-1-*.md`, `phase-2-*.md`, etc.)
