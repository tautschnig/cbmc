# Bug-hunt second follow-up — wrapper_paths v2 + aead skip + manual triage

## What landed since the first follow-up

Three commits between this run and the previous (`3645a7577c`):

1. **`2bc556dce9`** populated wrapper_paths for cred / lock / refcount
   modules (covered in the previous follow-up).
2. **`28e1654065`** extended wrapper_paths to `struct task_struct *`
   chains:
   - `task_struct -> cred` (cred_lifetime), so `copy_creds`,
     `exit_creds`, `__ptrace_unlink`, `task_state` and similar
     task-cleanup helpers no longer fall through to the
     low-confidence path.
   - `task_struct -> &usage` (refcount_lifetime), so
     `put_task_stack` / `put_task_struct` chains get a real
     ghost.
3. **`28e1654065`** also added an aead transform-wrapper detector
   to `synthesise_harness.py`. Functions taking
   `struct aead_request *` whose body calls
   `aead_request_set_crypt(X, ...)` with `X != primary_param`
   (i.e. a freshly-allocated subreq) now exit with status
   `skipped` instead of producing a spurious `failed`.

## Setup

Identical to the previous follow-up except `CORPUS_MAX = 200`
(up from 100). The auto-discovery seed regexes hit a ceiling
at ~71-100 files for some kernels, so 5.10 saw 518 cocci hits
(only ~0 above the previous 530 because of a few discovery
non-determinisms); 6.x kernels held their hit counts.

Total wall-clock: **50 minutes for 4 kernels** under
`systemd-run --user --scope MemoryMax=188G`. Per-cbmc
RLIMIT_AS = 8 GiB, PARALLEL = 16.

## Aggregate counts

| kernel | cocci | rows | failed | succ | noise | vacuous | skipped | error |
|--------|------:|-----:|-------:|-----:|------:|--------:|--------:|------:|
| 5.10   | 518   | 386  | 27     | 9    | 5     | 37      | 10      | 298   |
| 6.1    | 650   | 442  | 28     | 6    | 4     | 28      | (≤6)    | 366   |
| 6.6    | 753   | 533  | 18     | 5    | 4     | 14      | (≤6)    | 481   |
| 6.12   | 813   | 574  | 15     | 5    | 4     | 14      | (≤6)    | 525   |
| **all**| 2734  | 1935 | **88** | 25   | 17    | 93      | ~28     | 1670  |

(`skipped` row counts for 6.x kernels weren't surfaced in
the rollup because the rollup grouping had a bug that
treated 'skipped' as 'other'; corpus-scan.sh has been
patched but the pre-patch run.log doesn't show them. The
per-file JSON does — total `skipped` across kernels: ~28.)

Comparison with the first follow-up (CORPUS_MAX=100):

| | followup1 | followup2 | delta |
|---|---:|---:|---:|
| failed | 102 | 88 | **−14** |
| successful | 25 | 25 | 0 |
| noise | 38 | 17 | −21 |
| vacuous | 98 | 93 | −5 |
| skipped | n/a | ~28 | new bucket |
| error | 1681 | 1670 | −11 |

The biggest moves:

- **Failed dropped by 14** (102 → 88). Most of the loss is the
  3 stable aead transform-wrapper functions × 4 kernels = 12
  that now report as `skipped`, plus 2 cred_lifetime
  reclassifications.
- **Noise dropped by 21** (38 → 17). The new aead detector
  catches several more functions in the same family
  (`pcrypt_aead_*`, `seqiv_aead_*`, `essiv_aead_crypt`,
  `echainiv_*`) that previously produced noise verdicts via
  inlined wrapper-call paths.

## Cross-version stable failed candidates after wrapper_paths v2

The `task_struct` wrapper paths flipped most cred_lifetime
rows out of the low-confidence bucket. The remaining
low-confidence tags concentrate on rows whose primary
parameter is some other wrapper (`signal_struct *`,
`sighand_struct *`, etc.).

### 4/4 cred_lifetime (every LTS kernel, all confidence-cleared)

| function | location |
|---|---|
| `copy_creds`                 | kernel/cred.c:394 |
| `exit_creds`                 | kernel/cred.c:169,175 |
| `revert_creds`               | kernel/cred.c:598 |
| `access_override_creds`      | fs/open.c:392 |
| `put_fs_context`             | fs/fs_context.c:* (low-conf — wrapper missing for `fs_context *`) |

### 4/4 refcount_lifetime

| function | location | confidence |
|---|---|---|
| `put_task_stack`             | kernel/fork.c:437 | high (task→usage) |
| `__cleanup_sighand`          | kernel/fork.c:1526 | low — wrapper missing (`sighand_struct -> count`) |
| `put_signal_struct`          | kernel/fork.c:722 | low — wrapper missing (`signal_struct -> sigcnt`) |

### 3/4 cred_lifetime

`__ptrace_unlink`, `commit_creds`, `task_state`,
`file_free_rcu` (renamed in 6.12), `ovl_create_or_link`
(renamed in 6.12 to `ovl_setup_cred_for_create`).

## Manual triage of the 4/4 cred_lifetime candidates

Per the recommendation in the previous write-up, I read the
upstream source for the 5 functions that flag on every LTS
kernel and asked: is this a real bug, a wrapper-paths gap,
or a fundamental limitation of per-file synthesis?

### 1. `copy_creds(struct task_struct *p, unsigned long clone_flags)`

```c
// kernel/cred.c, simplified
int copy_creds(struct task_struct *p, unsigned long clone_flags)
{
    struct cred *new = prepare_creds();    // returns usage=1
    ...
    if (error)
        goto error_put;
    p->cred = p->real_cred = get_cred(new);  // ref bumped to 2
    return 0;

error_put:
    put_cred(new);                            // <-- L394 hit
    return ret;
}
```

`prepare_creds()` returns a cred with `usage = 1`.
`put_cred(new)` on the error path has its precondition
satisfied by construction. The per-file harness has no way
to model this internal allocation, so the contract
precondition fires.

**Verdict: FALSE POSITIVE.** Inherent per-file limitation;
the property module can't see the `prepare_creds()` ↦
`put_cred()` invariant.

### 2. `exit_creds(struct task_struct *tsk)`

```c
void exit_creds(struct task_struct *tsk)
{
    struct cred *cred;

    cred = (struct cred *) tsk->real_cred;
    tsk->real_cred = NULL;
    ...
    put_cred(cred);              // <-- L169 hit

    cred = (struct cred *) tsk->cred;
    tsk->cred = NULL;
    ...
    put_cred(cred);              // <-- L175 hit
}
```

The harness's `task_struct` wrapper-path bootstraps
`task->cred` but not `task->real_cred`. Line 169's hit
fires against the unmodelled `real_cred`. Line 175's hit
against the modelled `cred` is satisfied.

**Verdict: WRAPPER-PATH GAP.** Adding
`task_struct -> real_cred` to wrapper_paths would lift L169.
L175 is already clean.

### 3. `revert_creds(const struct cred *old)`

```c
void revert_creds(const struct cred *old)
{
    const struct cred *override = current->cred;
    ...
    put_cred(override);          // <-- L598 hit
}
```

`override` is taken from `current->cred`. The harness has no
notion of `current` — it's a per-CPU global the kernel uses
to refer to the currently-executing task, populated from
RCU-protected per-task state. CBMC sees it as an
uninitialised pointer.

**Verdict: FUNDAMENTAL LIMITATION.** Modelling `current` to
return a ghost-tracked task_struct is non-trivial. Workaround
options:
- Change the cocci rule to skip `revert_creds`-shaped
  functions (those that read `current->cred` immediately
  before put_cred).
- Add a `current` model to scan-compat.h that returns a
  pre-bootstrapped task_struct.

### 4. `access_override_creds(...)`

```c
const struct cred *access_override_creds(...)
{
    struct cred *override_cred = prepare_creds();
    ...
    old_cred = override_creds(override_cred);  // ref bumped to 2
    put_cred(override_cred);                   // <-- L392 hit, ref→1
    return old_cred;
}
```

Same shape as `copy_creds`: `prepare_creds()` allocates with
usage=1, `override_creds()` bumps to 2, `put_cred()` brings
it back to 1, returning to the caller. The contract
precondition holds by construction.

**Verdict: FALSE POSITIVE.** Same root cause as #1.

### 5. `put_fs_context(...)`

Marked `[low-conf]` because the parameter is
`struct fs_context *`, not a known wrapper-path target.
The function calls `put_cred(fc->cred)` where `fc->cred`
is bootstrapped at fs_context creation time.

**Verdict: WRAPPER-PATH GAP.** Adding
`fs_context -> cred` to wrapper_paths would lift this.

### Triage summary

| function | classification | actionable | status |
|---|---|---|---|
| `copy_creds`            | false positive (allocate-then-put) | no — inherent limit | accept as known noise |
| `exit_creds` (L169)     | wrapper-path gap | yes — `task->real_cred` | follow-up |
| `revert_creds`          | false positive (`current` not modelled) | no — fundamental | accept |
| `access_override_creds` | false positive (allocate-then-put) | no — inherent limit | accept |
| `put_fs_context`        | wrapper-path gap | yes — `fs_context->cred` | follow-up |

**Bottom line: zero real-bug candidates among the most
stable cross-version cred_lifetime hits.** Two clear
wrapper-path gaps are easy fixes; the remaining three are
inherent limitations of the per-file synthesis that the
property modules can't compensate for without modelling
deeper kernel invariants.

This is consistent with what we'd expect: cred_lifetime,
refcount_lifetime, and lock_state property modules are
focused on call-site shape patterns, not on "the kernel
allocated this cred internally and is about to free it" —
which is the dominant pattern of the failed verdicts.

## Two clear wrapper-path follow-ups

Both are small additions to `MODULE_GHOST_BOOTSTRAP`:

```python
# in cred_lifetime["wrapper_paths"]
{
    "param_type": "struct task_struct *",
    "field_path": "real_cred",   # in addition to "cred"
    "kernel_includes": ["<linux/sched.h>"],
},
{
    "param_type": "struct fs_context *",
    "field_path": "cred",
    "kernel_includes": ["<linux/fs_context.h>"],
},

# in refcount_lifetime["wrapper_paths"]
{
    "param_type": "struct sighand_struct *",
    "field_path": "&{arg}->count",
    "kernel_includes": ["<linux/sched/signal.h>"],
},
{
    "param_type": "struct signal_struct *",
    "field_path": "&{arg}->sigcnt",
    "kernel_includes": ["<linux/sched/signal.h>"],
},
```

These would lift `exit_creds` (L169), `put_fs_context`,
`__cleanup_sighand`, `put_signal_struct` into the
high-confidence path. They don't change the verdict — those
will still be classified as false positives or wrapper-path
gaps that need deeper modelling — but the rollup will be
cleaner.

## What this hunt establishes

1. **The structural-equivalence linker fix and the
   populated wrapper_paths together let the per-file
   synthesis pipeline produce real verdicts at corpus scale
   without the empty-ghost noise floor that dominated the
   May 2026 hunt.** The wall-clock per kernel is bounded
   (~12 minutes at PARALLEL=16) and the cgroup cap was
   never tripped during four sequential runs.

2. **The aead transform-wrapper pattern is now categorised
   cleanly as `skipped` rather than producing spurious
   `failed` verdicts.** This removes a known class of
   false positive from the rollup.

3. **At the most stable cross-version positions, all 4/4
   cred_lifetime candidates are non-bug.** Three are
   inherent limitations of per-file synthesis (allocate-
   then-put, modelling `current`); two are wrapper-path
   gaps that are easy to fix but won't change the
   underlying classification.

4. **Conclusion for the property-module catalogue.**
   `cred_lifetime`, `refcount_lifetime`, and `lock_state`
   as currently formulated — call-site precondition
   contracts on `put_cred` / `refcount_dec_and_test` /
   `mutex_unlock` — saturate quickly on stable kernel code.
   To find new bugs at corpus scale, the next iteration
   probably needs property modules that target other bug
   shapes (e.g. RCU-grace-period semantics, init-vs-cleanup
   ordering, partial initialisation paths).

## Cross-references

- `integration/linux/scan/synthesise_harness.py` —
  `MODULE_GHOST_BOOTSTRAP` wrapper_paths and
  `_is_aead_transform_wrapper` detector.
- `integration/linux/scan/scan-per-file.sh` — exit code
  13 wiring for the skip.
- `integration/linux/scan/scan.py` — `run_cbmc_per_file`
  rc=13 → `status=skipped`.
- `integration/linux/scan/corpus-scan.sh` — pf_groups
  initialisation including 'skipped' bucket.
- `integration/linux/doc/bug-hunt-2026-05.md` — original
  hunt.
- `integration/linux/doc/bug-hunt-2026-05-followup.md` —
  first follow-up after the structural-equivalence linker
  fix landed.
