# Corpus run on the 19-module catalog — Phase-3-vs-extension decision

This document records the corpus pass we ran on the full
post-Phase-1-and-2 catalog (19 modules) across all four LTS
kernels, the resulting verdict distribution, the hand-triage of
the most-stable cross-version candidates, and the resulting
recommendation for what to invest in next.

## Run setup

- All four LTS kernels: 5.10, 6.1, 6.6, 6.12.
- `CORPUS_MAX=200`, `EXTRA_SCAN_ARGS=--per-file`.
- Memory cap: `systemd-run --user --scope MemoryMax=188G
  MemorySwapMax=0` (= 25 % of the 755 GB host).
- Per-CBMC `RLIMIT_AS = 8 GiB`; `PARALLEL = 16`;
  `SCAN_FILE_TIMEOUT = 600`.
- Total wall-clock: **2 h 23 min** (5.10: 37 min, 6.1: 38 min,
  6.6: 34 min, 6.12: 31 min).
- Corpus discovery extended to cover all 19 modules' put-API
  patterns — 16 new entries in `corpus-scan.sh::CORPUS_PATTERNS`.

## Aggregate verdict distribution

| kernel | cocci | failed | succ | noise | vacuous | skipped | error |
|--------|------:|-------:|-----:|------:|--------:|--------:|------:|
| 5.10   |  2358 |     88 |   28 |    30 |      87 |      25 |  1026 |
| 6.1    |  2672 |    104 |   27 |    34 |      92 |      22 |  1175 |
| 6.6    |  2675 |     76 |   23 |    21 |      72 |      17 |  1275 |
| 6.12   |  2907 |     66 |   20 |    19 |      61 |      16 |  1409 |
| **all**| 10612 |    **334** |   98 |   104 |     312 |      80 |  4885 |

Comparison with the 8-module May-2026 follow-up #2:

| | followup2 (8 modules) | this run (19 modules) | delta |
|---|---:|---:|---:|
| failed     |  88 |  334 | +246 (×3.8) |
| successful |  25 |   98 | +73  (×3.9) |
| noise      |  17 |  104 | +87  (×6.1) |
| vacuous    |  93 |  312 | +219 (×3.4) |
| skipped    |  28 |   80 | +52  (×2.9) |
| error      | 1670 | 4885 | +3215 (×2.9) |

The catalog-doubling roughly tripled all per-file row counts
(consistent with the 19-vs-8 module factor minus the modules
that haven't surfaced any cocci hits in the scope: `aead`,
`pipe_buffer`, `page_provenance`, `alloc_tag`,
`rcu_critical_section`, `cancel_work_before_free`).

## Failed candidates by module

| module                     | failed | low-conf | successful |
|----------------------------|-------:|---------:|-----------:|
| lock_state                 |    121 |      121 |         38 |
| inode_lifetime (Phase 1)   |     68 |       36 |          5 |
| device_lifetime (Phase 1)  |     50 |       45 |         16 |
| cred_lifetime              |     41 |       19 |         13 |
| dentry_lifetime (Phase 1)  |     28 |       23 |          5 |
| refcount_lifetime          |     14 |       12 |          4 |
| fput_lifetime  (Phase 1)   |      6 |        6 |          3 |
| kobject_lifetime           |      5 |        5 |         12 |
| sock_lifetime  (Phase 1)   |      1 |        1 |          2 |

**No failed verdicts from**: aead, page_provenance, pipe_buffer,
alloc_tag (existing, narrow scope); skb_lifetime, module_lifetime,
kref_lifetime (Phase 1 / 2, sparse cocci hits in our scope);
rcu_critical_section, cancel_work_before_free (Phase 2,
intentionally not wired into per-file synthesis).

**Of the 334 failed verdicts, 268 (80%) are low-confidence**
(`[empty-ghost: low-confidence]` — the harness's parameter
wasn't directly the bug-class type and no `wrapper_paths`
entry covered it).  Only 66 are high-confidence.

## High-confidence cross-version-stable candidates

After filtering out low-confidence:

### 4/4-stable (all four LTS kernels)

| module           | function                       | classification             |
|------------------|--------------------------------|----------------------------|
| cred_lifetime    | copy_creds                     | FALSE POSITIVE (allocate-then-put)  |
| cred_lifetime    | exit_creds                     | known: contract holds since wrapper_paths v3 lifted L175; L169 still flags real_cred put |
| cred_lifetime    | put_fs_context                 | FALSE POSITIVE (put on fc->cred where fc was allocated internally) |
| device_lifetime  | attribute_container_release    | **WRAPPER-PATH GAP** (`device → parent`) |
| inode_lifetime   | ext2_link                      | **WRAPPER-PATH GAP** (`dentry → d_inode`) |

### 3/4-stable

* **cred_lifetime**: `__ptrace_unlink`, `commit_creds`,
  `task_state` — known cred allocate-then-put / current-using
  shapes.
* **inode_lifetime**: `__anon_inode_getfile`, `affs_create`,
  `affs_mkdir`, `affs_new_inode`, `affs_symlink`, `bfs_create`,
  `swap_inode_boot_loader` — **all match the `inode = new_inode(sb)`
  /* error */ `iput(inode)` allocate-then-put pattern**.

### Hand-triage details

```c
// device_lifetime: attribute_container_release
static void attribute_container_release(struct device *classdev)
{
    struct internal_container *ic =
        container_of(classdev, struct internal_container, classdev);
    struct device *dev = classdev->parent;
    kfree(ic);
    put_device(dev);
}
```
The harness inits `classdev` ghost; `classdev->parent` is
zero-init NULL.  The contract `device_live(parent) == 1` fires.
**Adding `device → parent` to wrapper_paths converts this to
high-confidence-with-bootstrap.**  Whether the kernel actually
holds a parent reference is a separate kernel-invariant
question — the contract says "yes by construction".

```c
// inode_lifetime: ext2_link
static int ext2_link(struct dentry *old_dentry, struct inode *dir,
                     struct dentry *dentry)
{
    struct inode *inode = d_inode(old_dentry);
    ihold(inode);
    err = ext2_add_link(dentry, inode);
    if (!err) { d_instantiate(dentry, inode); return 0; }
    inode_dec_link_count(inode);
    iput(inode);
}
```
`d_inode(old_dentry)` extracts the inode from the dentry; the
harness has no path bootstrapping it.  **Adding
`dentry → d_inode` to wrapper_paths fixes this.**

```c
// inode_lifetime: affs_create  (representative of the
//                                allocate-then-put class)
int affs_create(struct inode *dir, struct dentry *dentry, ...)
{
    struct inode *inode = affs_new_inode(dir);   // allocates
    ...
    if (error) { iput(inode); return error; }    // <-- false-positive
    return 0;
}
```
**Inherent per-file synthesis limit.** Per-file harness can't
model `affs_new_inode`'s internal allocation that returns a
usage=1 inode.  Same class as `copy_creds`, `access_override_creds`.

## Wrapper-paths gap: top first-param types in low-confidence rows

| count | module           | first-param type            | wrapper path needed |
|------:|------------------|-----------------------------|---------------------|
|    16 | lock_state       | `struct inode *`            | `inode → &i_rwsem` (or filesystem-specific lock) |
|    10 | inode_lifetime   | `struct super_block *`      | `super_block → s_root->d_inode` (two-level) |
|     6 | lock_state       | `struct device *`           | `device → &mutex` |
|     5 | lock_state       | `struct mnt_idmap *`        | newer kernels; less common |
|     3 | device_lifetime  | `struct klist_node *`       | container_of(...).device  |
|     3 | device_lifetime  | `struct device_driver *`    | not directly addressable |
|     3 | dentry_lifetime  | `struct super_block *`      | `super_block → s_root` |
|     3 | dentry_lifetime  | `struct ovl_fs *`           | overlayfs-specific |
|     3 | lock_state       | `struct work_struct *`      | container_of pattern |
|     2 | cred_lifetime    | `struct rcu_head *`         | `file_free_rcu` shape; container_of |

**~50 rows** would lift to high-confidence with the top three
wrapper-path additions (`inode → &i_rwsem`, `device → &mutex`,
`super_block → s_root`).  None of these would change the verdict's
bug/no-bug classification — they'd just put high-confidence
failed verdicts on the same rows that today produce low-confidence
failed verdicts.

The remaining 218 low-confidence rows split between
filesystem-specific lock paths (e.g. `dir → i_sb → BFS_SB(...) →
bfs_lock`) and allocate-then-put shapes (NULL after zero-init
sentinel).

## Productivity assessment per module

The signal-to-noise ratio per Phase-1+2 module:

* **`inode_lifetime`** (most productive new module): 68 failed,
  32 high-confidence; the high-confidence rows are dominated
  by `affs_*` / `bfs_*` / `__anon_inode_getfile` allocate-then-put
  patterns plus `ext2_link` (wrapper-path gap).  No genuine
  bug candidates surfaced after triage.
* **`device_lifetime`**: 50 failed, 5 high-confidence; the
  one stable function is `attribute_container_release` —
  wrapper-path gap.
* **`dentry_lifetime`**: 28 failed, 5 high-confidence; minor
  contributor.
* **`fput_lifetime`**, **`sock_lifetime`**, **`skb_lifetime`**,
  **`module_lifetime`**, **`kref_lifetime`**: 0-6 failed each.
  Cocci hit count is too low in our scope (no drivers/* in
  the corpus regex).
* **`rcu_critical_section`**, **`cancel_work_before_free`**: 0
  failed in per-file mode (intentional; they only run in
  property-module-native flow which `--per-file` skips).

**Bug-finding rate**: of the **66 high-confidence failed
verdicts** across 4 kernels, after hand-triage every
classifiable one is either:
- A **wrapper-path gap** (~10 candidates): would resolve with
  wrapper_paths v4.
- An **allocate-then-put false positive** (~50 candidates):
  inherent per-file synthesis limit; no fix without deeper
  modelling of internal allocators.
- A **`current`-using function** (~5 candidates): already
  skipped via the `_uses_current_macro` detector for some,
  remaining ones use `current` indirectly via macros that the
  detector doesn't catch.

**No genuinely-new-bug candidate has been surfaced by the 11
new modules** at this corpus scale.  The 5/3-pattern of "real
candidates" is the same pattern we hit in May 2026 with 8
modules.

## What this means

The data validates two key hypotheses and refutes one:

1. **Confirmed**: per-file synthesis at corpus scale on the
   balance shape **saturates against stable kernel code**.
   Doubling the catalog of balance-shape modules (8 → 12) and
   adding novel ghost shapes (kref + rcu + cancel_work, → 19
   total) didn't surface new bug candidates.

2. **Confirmed**: the wrapper-paths gap is **massive** (80% of
   failed verdicts).  Filling the top three wrapper paths would
   convert ~50 rows to high-confidence — useful for triage
   readability, not for finding new bugs.

3. **Refuted**: that adding more balance modules of similar
   shape would produce more bug candidates.  Instead, we get
   more rows of the same false-positive classes.

## Decision: catalog extension or Phase 3?

Given the data, **Phase 3 is the right next step**.

Reasoning:

- **Catalog extension is bounded.**  Adding pid_lifetime,
  key_lifetime, dst_lifetime, etc. would produce more rows of
  the same false-positive classes the existing 12 balance
  modules already do.  Marginal CVE coverage gain per new
  module is single-digit mentions.

- **Wrapper-paths v4 is worth doing but not as a next phase.**
  ~30 minutes of work on the top 3 wrapper paths
  (`inode → &i_rwsem`, `device → &mutex`,
  `super_block → s_root`) cleans up the rollup but does NOT
  find new bugs.  Best done as a follow-up commit, not as a
  phase.

- **Phase 3 unlocks bug categories the catalog cannot reach.**
  The CVE survey's `out_of_bounds` (649 CVEs, 7.4%),
  `race_or_toctoue` (232 CVEs, 2.7%), and `bpf_verifier_or_runtime`
  (28 CVEs but high-severity) are not addressable by any
  refcount-balance module — no matter how many we add.

- **`netlink_attr_validation` is the highest-leverage Phase 3
  pick.**  649 CVEs in `out_of_bounds` is bigger than the
  entire balance catalog combined.  The shape (size-tainted
  ghost on `nla_data`/`nla_get_*` returns) introduces a new
  ghost category that's reusable for other bounds-checking
  modules.

## Recommended sequence

1. **Wrapper-paths v4 quick-win** (~1 hour):
   - `inode → &i_rwsem` (lock_state).
   - `device → &mutex` (lock_state).
   - `dentry → d_inode` (inode_lifetime).
   - `device → parent` (device_lifetime).

   Cleans up ~50 of the 268 low-confidence rows.  Doesn't find
   new bugs but improves rollup readability.

2. **Phase 3a: `netlink_attr_validation`** (~2-3 days).
   New ghost shape: per-`struct nlattr *` size annotation.
   Contract on `nla_data(attr)` returns: requires
   `nla_size_at_least(attr, sizeof(...)) == 1`.  Cocci finds
   `nla_get_*` call sites; CBMC checks bounds.
   Targets `net/netlink/`, `net/netfilter/`, parts of `net/`
   in general.  Highest-volume Phase-3 candidate.

3. **Phase 3b: `concurrent_pointer_publish`** (~3-4 days).
   Uses CBMC's concurrency support.  Per the earlier user
   note, CBMC concurrency is slow but tractable.  Worth a
   benchmarking spike before committing.

4. **Phase 3c: `bpf_helper_arg_validation`** (deferred).
   Depends on the BPF front-end CBMC has in other work being
   wired into this checkout.  Investigate availability first.

## Honest caveats

* **The 4-LTS corpus is biased toward fs/, kernel/, net/.**
  The corpus discovery patterns deliberately exclude
  drivers/* because those are rarely buildable under our
  allnoconfig setup.  This biases the verdict distribution.
  drivers/-heavy modules (sock, skb, fput, module, kref) get
  fewer cocci hits than they would on a broader scope.

* **CORPUS_MAX=200 is a per-pattern cap.**  With 19 modules
  contributing patterns, the EFFECTIVE corpus per kernel is
  somewhere between 200 (one-module-per-file) and 200 × 19
  (independent files per module).  In this run we saw
  165-175 distinct files per kernel.

* **80% low-confidence is not unique to new modules.**  The
  EXISTING `lock_state` module had the same low-confidence
  rate before Phase 1; we just have more total rows now.

## Reproducing

```sh
# 1. Build/refresh.
cmake --build build --target cbmc goto-cc goto-instrument -j4

# 2. Run all 4 LTS kernels under cgroup cap.
RUN_BASE=/tmp/phase2-corpus-$(date +%Y-%m-%d)
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

- [CVE survey](cve-survey-2023-2026.md) — the prioritisation
  evidence.
- [Phase 1](phase-1-balance-modules-2026-05.md) — factory + 8
  balance modules.
- [Phase 2](phase-2-rcu-cancel-2026-05.md) — kref, RCU, cancel-
  before-free.
- [`balance_module_factory.py`](../scan/balance_module_factory.py)
- [`scan/corpus-scan.sh`](../scan/corpus-scan.sh) — extended this
  run with patterns for all 19 modules.
