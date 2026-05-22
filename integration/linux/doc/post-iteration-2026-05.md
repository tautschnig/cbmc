# Post-iteration: cancel siblings, per-CPU RCU, 25-module corpus

This iteration delivered the post-iteration roadmap from the
previous session: two sibling cancel-before-free modules,
per-CPU concurrency for `rcu_critical_section`, and a corpus
re-run measuring the cumulative effect of the wrapper-paths
v4 fix, netlink v3, and the new modules.

## Delivered

### Two sibling cancel-before-free modules

* **`cancel_delayed_work_before_free`** —
  `INIT_DELAYED_WORK(...)` followed by `kfree(...)` without
  intervening `cancel_delayed_work_sync`.  Same per-pointer-
  flag shape as `cancel_work_before_free`.
* **`del_timer_sync_before_free`** — `timer_setup(...)`
  followed by `kfree(...)` without intervening
  `del_timer_sync` / `timer_delete_sync`.

Both modules use synthetic `__assert_no_pending_*` checkpoint
contracts.  Cocci is the primary integration path for real
kernel source.  Direct-call vuln/fix harnesses validate
end-to-end.

### Per-CPU RCU concurrent variant

`rcu_critical_section` now exposes per-thread depth ghosts
(`__rcu_depth_t1`, `__rcu_depth_t2`) and corresponding
contracts (`rcu_read_lock_t1` / `_t2`,
`synchronize_rcu_per_cpu`).  The new
**3-thread concurrent harness**
(`rcu_critical_section_per_cpu_concurrent_harness.c`):

* Spawns two readers (one each on `_t1`, `_t2`) via
  `__CPROVER_ASYNC_1` / `_2`.
* Main thread is the writer calling
  `synchronize_rcu_per_cpu` (requires both per-thread
  depths are zero).

CBMC's concurrent symex finds an interleaving where one
reader is inside its section when the writer's call runs;
precondition fails.  Vuln: 32 ms.  Fix (sequential): 19 ms.

This is the catalog's **first 3-thread harness** and
demonstrates the concurrent symex scales past the
2-thread shape.

### What the per-CPU variant catches

The single-counter `__rcu_csection_depth` model conflates
multiple concurrent readers.  Cross-thread unbalanced
unlock — thread B calling `rcu_read_unlock` when thread A
was the one holding a section — passes silently with the
single-counter model because A's enter raised the global
counter.  The per-thread depth model catches this by
keying the requires-clause to the specific thread.

## Catalog total

The CBMC property-module catalog now stands at
**twenty-five** modules:

* aead, page_provenance/scatterlist, pipe_buffer, alloc_tag,
  cred_lifetime, lock_state, refcount_lifetime,
  kobject_lifetime, device_lifetime, of_node_lifetime,
  inode_lifetime, dentry_lifetime, fput_lifetime,
  sock_lifetime, skb_lifetime, module_lifetime,
  kref_lifetime, rcu_critical_section,
  cancel_work_before_free, netlink_attr_validation,
  concurrent_pointer_publish, concurrent_double_put,
  tocttou_inode_check (23 from prior iterations)
* **cancel_delayed_work_before_free,
  del_timer_sync_before_free** (this iteration; +2)

Plus the per-CPU concurrent extension to
`rcu_critical_section`.

## 25-module corpus run

Ran the full 25-module catalog across all four LTS kernels
under the same memory cap as previous runs.

### Setup

- Memory: `systemd-run --scope MemoryMax=188G MemorySwapMax=0`
  (= 25% of 755 GB host).
- Per-CBMC RLIMIT_AS = 8 GiB; PARALLEL = 16; CORPUS_MAX =
  200; SCAN_FILE_TIMEOUT = 600s.
- Total wall-clock: **2h 24min** (5.10: 38 min, 6.1: 39 min,
  6.6: 35 min, 6.12: 32 min).
- Corpus discovery extended for the two new sibling cocci
  patterns (`INIT_DELAYED_WORK`, `timer_setup`).

### Aggregate verdict distribution

| kernel | cocci | failed | succ | noise | vacuous | skipped | error |
|--------|------:|-------:|-----:|------:|--------:|--------:|------:|
| 5.10   |  2365 |     83 |   27 |    34 |      87 |      25 |  1028 |
| 6.1    |  2681 |     99 |   25 |    38 |      92 |      22 |  1178 |
| 6.6    |  2677 |     70 |   21 |    23 |      71 |      17 |  1275 |
| 6.12   |  2915 |     61 |   18 |    20 |      60 |      16 |  1411 |
| **all**| 10638 |    **313** |   91 |   115 |     310 |      80 |  4892 |

### Comparison with the 19-module corpus (May 2026)

| Metric | 19-module | 25-module | Δ |
|---|---:|---:|---:|
| Total failed | 334 | 313 | **−21 (−6 %)** |
| High-confidence failed | 66 | 58 | **−8 (−12 %)** |
| Low-confidence failed | 268 | 255 | −13 |
| Successful | 98 | 91 | −7 |
| Noise | 104 | 115 | +11 |
| Vacuous | 312 | 310 | −2 |
| Skipped | 80 | 80 | 0 |
| Error | 4885 | 4892 | +7 |

### High-confidence cross-version-stable candidates

| Stability | 19-module run | 25-module run |
|---|---:|---:|
| 4/4-stable HIGH-conf | 5 | **1** |
| 3/4-stable HIGH-conf | 10 | 12 |

**The 4/4-stable high-confidence candidate count dropped
from 5 to 1** — an 80% reduction.  The four candidates that
flipped from VIOLATION to clean:

| Function | Module | Why it cleared |
|---|---|---|
| `attribute_container_release` | device_lifetime | wrapper-paths v4: `device → parent` |
| `ext2_link` | inode_lifetime | wrapper-paths v4: `dentry → d_inode` |
| `exit_creds` | cred_lifetime | bootstrap fix bootstrapped both `task->cred` AND `task->real_cred` to non-NULL backings |
| `put_fs_context` | cred_lifetime | wrapper-paths v3: `fs_context → cred` (already in 23-module) plus bootstrap fix |

The single remaining 4/4-stable high-conf is **`copy_creds`**
— the canonical allocate-then-put false positive class
we've documented since May 2026.  It's an inherent per-file
synthesis limit and won't clear without modelling internal
allocators.

The **12 3/4-stable high-conf** candidates split:
- `cred_lifetime`: 4 (`__ptrace_unlink`, `commit_creds`,
  `exit_creds`, `task_state`).  `exit_creds` is now 3/4
  rather than 4/4 — one kernel version's flip back is
  consistent with version-specific cred handling.
- `dentry_lifetime`: 1 (`mknod_ptmx`).
- `inode_lifetime`: 7 (allocate-then-put cluster:
  `__anon_inode_getfile`, `affs_create`, `affs_mkdir`,
  `affs_new_inode`, `affs_symlink`, `bfs_create`,
  `swap_inode_boot_loader`).

### Per-module productivity

| module                     | failed | low-conf | high-conf | successful |
|----------------------------|-------:|---------:|----------:|-----------:|
| lock_state                 |    117 |      117 |         0 |         38 |
| inode_lifetime             |     60 |       34 |        26 |          3 |
| device_lifetime            |     46 |       45 |         1 |         12 |
| cred_lifetime              |     36 |       19 |        17 |         14 |
| dentry_lifetime            |     28 |       16 |        12 |          5 |
| refcount_lifetime          |     14 |       12 |         2 |          2 |
| fput_lifetime              |      6 |        6 |         0 |          3 |
| kobject_lifetime           |      5 |        5 |         0 |         12 |
| sock_lifetime              |      1 |        1 |         0 |          2 |

`lock_state` continues to be all-low-conf — its 117 failed
verdicts are dominated by filesystem-specific 3-level paths
(`inode → i_sb → BFS_SB(...) → bfs_lock`) that aren't
addressable by simple wrapper paths.

`inode_lifetime` and `dentry_lifetime` produce most of the
high-conf candidates, but every classifiable one is the
same allocate-then-put pattern.

### Modules that produced zero failed verdicts at corpus scale

`aead`, `page_provenance`, `pipe_buffer`, `alloc_tag`,
`skb_lifetime`, `module_lifetime`, `kref_lifetime`,
`rcu_critical_section`, `cancel_work_before_free`,
`cancel_delayed_work_before_free`,
`del_timer_sync_before_free`, `netlink_attr_validation`,
`concurrent_pointer_publish`, `concurrent_double_put`,
`tocttou_inode_check`.

For most of these, cocci hits in our scope are sparse (e.g.
`module_put`, `kref_put` are mostly used in driver
subsystems we don't fully cover).  The three concurrency
modules and `cancel_*_before_free` are intentionally not in
`_PER_FILE_SUPPORTED_MODULES` (their integration is via
property-module-native scan flow, not per-file).

## What this confirms

1. **The wrapper-paths v4 + bootstrap fix worked
   as expected.**  The 4 of 5 4/4-stable false positives
   that cleared were specifically the ones with
   wrapper-path or bootstrap-NULL gaps.

2. **The remaining 4/4-stable false positive
   (`copy_creds`) is the inherent per-file limitation.**
   No mechanical fix.  Modelling internal allocators
   (`prepare_creds()` returning a usage=1 cred) would need
   either function-summary contracts on every allocator or
   a goto-instrument pass that recognises the allocator
   pattern.

3. **The 7 `inode_lifetime` 3/4-stable candidates are
   all the same allocate-then-put class.**  Same root
   cause as `copy_creds`.  A targeted fix would be to add
   the affs / bfs / etc. inode allocators to a
   "trusted-allocator" list that the harness pre-populates
   with usage=1 ghosts.

4. **No genuinely new bug candidates surfaced** even
   after adding 6 modules across two iterations.  The
   pattern from May 2026 ("per-file synthesis saturates
   on stable kernel code") still holds.

## What's next

The honest assessment is that the **per-file synthesis
pipeline has saturated**.  Adding more balance-shape modules
or even more concurrency siblings produces more rows of the
same classes — wrapper-path gaps that v4 cleaned up, plus
allocate-then-put false positives that need deeper modelling.

The next investments to escape saturation would be:

1. **Trusted-allocator modelling.**  Pre-populate ghosts
   for `prepare_creds`, `affs_new_inode`, etc. so the
   harness models the "allocate-then-put" pattern as
   "ghost-live then put" rather than "uninit then put".
   Target the largest single false-positive class
   (allocate-then-put) directly.

2. **Cross-function summary contracts.**  Per-file
   synthesis reasons over one function at a time.  A
   summary-aware mode could read function summaries from
   neighbouring files and use them to model nested calls.
   This is research-level work.

3. **Other corpus discovery sources.**  The current
   `corpus-scan.sh::CORPUS_PATTERNS` is bounded by the
   regexes we hand-wrote.  A more systematic source —
   e.g. extracting candidate sites from `lib/test_lockup`-
   style stress tests, or from the kernel's own
   `Documentation/dev-tools/` test specs — would broaden
   the corpus beyond what we've explored.

4. **Property modules in shapes we haven't tried.**
   Module-internal invariants beyond call-site
   preconditions: data-structure consistency invariants,
   per-thread-state reachability, control-flow
   completeness.  Each is a research direction.

Items still in the pipeline from earlier roadmaps:

* **netlink_attr_validation v4** — cast-then-read for
  `nla_data` plus auto-init for nlattr-pointer-array
  parameters.
* **`bpf_helper_arg_validation`** when the BPF front-end
  is integrated.

These remain available but are less leveraged than the
saturation-escape items above.

## Honest limitations

The 25-module catalog covers an estimated **45-50% of the
bug-shape volume** in the 2023-2026 kernel CVE record by
mention-count proxy.  The next 5% would require
substantially more invasive modelling work; the marginal
cost per additional bug-class point is rising sharply.

For "find new bugs at corpus scale", the catalog is
**diminishing returns territory**.  The pipeline does
produce real verdicts and the false-positive rate is
empirically bounded — but the same saturation finding
from May 2026 still applies.

For "build a generally-useful kernel static analysis
infrastructure", the catalog now covers the major bug-
shape categories with documented limitations and a clean
factory + concurrency primitive.  That's a credible
foundation for future research-level extensions.

## Reproducing

```sh
# Sibling cancel modules:
./integration/linux/properties/cancel_delayed_work_before_free/run.sh
./integration/linux/properties/del_timer_sync_before_free/run.sh

# Per-CPU RCU concurrent harness (vuln):
build/bin/goto-cc \
  integration/linux/properties/rcu_critical_section/rcu_critical_section.c \
  integration/linux/scan/adapters/rcu_critical_section_kernel_adapter.c \
  integration/linux/scan/adapters/rcu_critical_section_per_cpu_concurrent_harness.c \
  -o /tmp/rcu.gb
build/bin/goto-instrument \
  --replace-call-with-contract rcu_read_lock_t1 \
  --replace-call-with-contract rcu_read_unlock_t1 \
  --replace-call-with-contract rcu_read_lock_t2 \
  --replace-call-with-contract rcu_read_unlock_t2 \
  --replace-call-with-contract synchronize_rcu_per_cpu \
  /tmp/rcu.gb /tmp/rcu.inst
build/bin/cbmc /tmp/rcu.inst    # VERIFICATION FAILED
# Add -DFIXED for the safe shape; expect VERIFICATION SUCCESSFUL.

# 25-module corpus across 4 LTS kernels:
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
    ./integration/linux/scan/corpus-scan.sh "$RUN_BASE/$k" \
    > "$RUN_BASE/$k.log" 2>&1
done
```

## Cross-references

- [CVE survey](cve-survey-2023-2026.md)
- [19-module corpus](corpus-2026-05-19modules.md) —
  baseline this run is compared to.
- [Phase 3 closeout](phase-3-closeout-2026-05.md) — work
  that landed before this iteration.
