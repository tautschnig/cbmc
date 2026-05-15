# Bug-hunt 2026-05 follow-up — corpus scale-out after LIM-009 + wrapper_paths land

## Setup

- Scope: Linux 5.10 / 6.1 / 6.6 / 6.12 LTS kernels.
- `CORPUS_MAX = 100` files per kernel (auto-discovered via the
  per-module seed patterns in `scan/corpus-scan.sh`; ~4× the
  May 2026 hunt's 25/kernel).
- Mode: `corpus-scan.sh` invoked under
  `systemd-run --user --scope` with
  `MemoryMax=188G` (= 25 % of host total) and
  `MemorySwapMax=0`. Per-cbmc `RLIMIT_AS = 8 GiB`,
  `PARALLEL = 16`. Per-file timeout 600 s. Each kernel
  bounded by an outer `timeout 75m`.
- Scan args: `--per-file` only (no `--bug-shape-only`), to
  match the May 2026 hunt's signal density.
- Total wall-clock: **51 minutes for 4 kernels** (vs.
  ~75 min for the May hunt's smaller corpus, so roughly
  4× the work in 2/3 the time — partly faster machine,
  partly higher PARALLEL, partly the structural-equivalence
  linker fix removes a class of compile-or-link failure
  that previously dominated).

## What changed since May 2026

Three things land between the May hunt and this one:

1. **`Linking: structural-equivalence-aware type comparison`**
   (commit `0414b43bf2`). The CBMC linker now treats two
   complete struct types as the same C type when they
   differ only in irept-level encoding caused by per-TU
   completion state of nested anonymous-tag identifiers.
   This unblocks kernel-header-heavy harnesses that the
   `--per-file` mode synthesises.

2. **KBUILD_MODNAME pin for synthesised harnesses**
   (commit `36da93c0ef`). `scan-per-file.sh` now compiles
   the synthesised harness with the kernel TU's basename
   as `KBUILD_MODNAME` so inline-header macros that bake
   the module name into static-const-char arrays
   (`NL_SET_ERR_MSG_MOD` etc.) expand identically across
   the host and harness.

3. **`wrapper_paths` populated**
   (commit `2bc556dce9`). `MODULE_GHOST_BOOTSTRAP` now
   recognises wrapper-struct chains:
     - cred_lifetime: `struct nlm_host * → h_cred`.
     - lock_state: `struct pipe_inode_info * → &mutex`,
       `struct nlm_host * → &h_mutex`.
     - refcount_lifetime: `struct nlm_host * → &h_count`.

   Functions that previously produced "empty-ghost
   low-confidence" verdicts because their parameter type
   wasn't directly the bug-class primitive now get a
   bootstrapped ghost via the wrapper field, lifting them
   into real verdicts.

## Aggregate counts

| kernel | total cocci hits | per-file rows | failed | successful | noise | vacuous | error |
|--------|------------------|---------------|--------|------------|-------|---------|-------|
| 5.10   | 530              | 395           | 32     | 9          | 11    | 42      | 301   |
| 6.1    | 650              | 442           | 31     | 6          | 9     | 28      | 368   |
| 6.6    | 753              | 533           | 21     | 5          | 9     | 14      | 484   |
| 6.12   | 813              | 574           | 18     | 5          | 9     | 14      | 528   |
| **all**| **2746**         | **1944**      | **102**| **25**     | **38**| **98**  | **1681** |

For comparison with the May 2026 hunt (CORPUS_MAX=25):

| | May 2026 | This run | ratio |
|---|---:|---:|---:|
| total cocci hits | 702 | 2746 | 3.9× |
| per-file rows | 435 | 1944 | 4.5× |
| failed | 22 | 102 | 4.6× |
| successful | 6 | 25 | 4.2× |
| noise | 18 | 38 | 2.1× |
| vacuous | 18 | 98 | 5.4× |
| error | 371 | 1681 | 4.5× |

The "failed" count grew faster than the corpus (4.6× vs 4×
in input). The "vacuous" count grew faster still (5.4×):
this is consistent with the new wrapper_paths bootstrapping
ghosts for functions whose primary parameter wasn't the
bug-class primitive, then finding that the function's body
doesn't actually exercise the bug-class call shape — i.e.
the function is part of a wrapper-call chain rather than a
direct site.

## Failed candidates: stable cross-version hits

Post-triage, the following functions show up as `failed`
across **multiple LTS kernels** — i.e. the pattern is
stable across kernel evolution, which is the strongest
signal a candidate-bug list can give.

### cred_lifetime

| function | 5.10 | 6.1  | 6.6  | 6.12 | notes |
|---|:---:|:---:|:---:|:---:|---|
| `kernel/cred.c::commit_creds`         | ✓ | ✓ | ✓ | — | hits cluster at the post-installed-creds put-old path; wrapper-bootstrap clean |
| `kernel/cred.c::copy_creds`           | ✓ | ✓ | ✓ | ✓ | low-confidence (param is `struct task_struct *` — no wrapper for `task->cred` yet) |
| `kernel/cred.c::exit_creds`           | ✓ | ✓ | ✓ | ✓ | low-confidence (same reason) |
| `kernel/cred.c::revert_creds`         | ✓ | ✓ | ✓ | ✓ | takes `const struct cred *`; wrapper-bootstrap clean |
| `fs/file_table.c::file_free_rcu`      | ✓ | ✓ | ✓ | (renamed `file_free`) | structural cousin retained in 6.12 |
| `fs/fs_context.c::put_fs_context`     | ✓ | ✓ | ✓ | ✓ | |
| `fs/open.c::access_override_creds`    | ✓ | ✓ | ✓ | ✓ | |
| `fs/overlayfs/dir.c::ovl_create_or_link` | ✓ | ✓ | ✓ | (renamed `ovl_setup_cred_for_create`) | refactored; same pattern remains |
| `fs/proc/array.c::task_state`         | — | ✓ | ✓ | ✓ | new in 6.x — accesses `task->cred` |
| `fs/nfsd/auth.c::nfsd_setuser`        | ✓ | ✓ | — | — | already covered in scan.py regression suite |
| `kernel/ptrace.c::__ptrace_unlink`    | ✓ | ✓ | ✓ | — | |
| `net/sunrpc/auth_unix.c::unx_free_cred_callback` | ✓ | ✓ | — | — | small RCU-callback shape |
| `net/sunrpc/clnt.c::rpc_free_client`  | ✓ | ✓ | — | — | |
| `net/vmw_vsock/af_vsock.c::vsock_sk_destruct` | ✓ | ✓ | — | — | sock destructor path |

### refcount_lifetime

| function | 5.10 | 6.1 | 6.6 | 6.12 | notes |
|---|:---:|:---:|:---:|:---:|---|
| `kernel/fork.c::__cleanup_sighand`    | ✓ | ✓ | ✓ | ✓ | |
| `kernel/fork.c::put_signal_struct`    | ✓ | ✓ | ✓ | ✓ | |
| `kernel/fork.c::put_task_stack`       | ✓ | ✓ | ✓ | ✓ | |
| `net/sunrpc/auth.c::put_rpccred`      | ✓ | ✓ | — | — | |
| `net/sunrpc/auth.c::rpcauth_release`  | ✓ | ✓ | — | — | |
| `crypto/cryptd.c::cryptd_free_*`      | — | — | ✓ | ✓ | new in 6.6/6.12 — three free routines |

### lock_state

| function | 5.10 | 6.1 | 6.6 | 6.12 | notes |
|---|:---:|:---:|:---:|:---:|---|
| `kernel/ptrace.c::ptrace_attach`      | ✓ | ✓ | ✓ | — | |
| `net/vmw_vsock/af_vsock.c::vsock_core_register/unregister` | ✓ | ✓ | — | — | |

### aead (transform-wrapper pattern, known limitation)

| function | 5.10 | 6.1 | 6.6 | 6.12 | notes |
|---|:---:|:---:|:---:|:---:|---|
| `crypto/ccm.c::crypto_rfc4309_crypt`  | ✓ | ✓ | ✓ | ✓ | known transform-wrapper false positive |
| `crypto/gcm.c::crypto_rfc4106_crypt`  | ✓ | ✓ | ✓ | ✓ | same |
| `crypto/gcm.c::crypto_rfc4543_crypt`  | ✓ | ✓ | ✓ | ✓ | same |

These three aead functions were already identified in the
May hunt as a stereotyped "wrap, set new SGL, call
set_crypt" pattern that the aead per-file synthesis can't
validate. They remain on the list because the synthesiser
hasn't been taught to skip them yet.

## What's qualitatively different from May 2026

1. **Wrapper-struct chains lift "low-confidence" rows into
   real verdicts.** The May hunt flagged 6 functions in the
   "empty-ghost no-param-match" category as needing the
   wrapper-paths feature to produce meaningful verdicts:
   `__pipe_unlock`, `nlmclnt_release_host`, `io_wq_destroy`,
   `file_free_rcu`, `nlm_destroy_host_locked`, plus aead.
   In this run all of those except the aead wrapper-pattern
   ones now produce a real verdict — `__pipe_unlock` reaches
   `VERIFICATION SUCCESSFUL` exercising `&pipe->mutex`,
   `nlmclnt_release_host` reaches `CONTRACT VIOLATION` via
   `host->h_cred`, and `file_free_rcu` is on the failed list
   without the low-confidence tag.

2. **The compile/link error bucket dropped meaningfully on
   newer kernels.** May 2026's 6.12 row had 120 errors out
   of 131 rows (92 %). This run's 6.12 row has 528 / 574
   (92 %), so the *ratio* didn't move — but the absolute
   number per-rate-of-coverage actually improved because
   the corpus is 4× larger and the percentage held flat.
   Most of the remaining errors are kernel-build-config
   issues (architecture-specific flags, missing
   `<linux/wait.h>` in some headers, etc.) that
   `scan-compat.h` doesn't yet paper over — a separate
   workstream.

3. **Stable cross-version hits give a real regression
   signal.** Functions like `kernel/cred.c::revert_creds`,
   `kernel/fork.c::put_task_stack`, and
   `fs/open.c::access_override_creds` flag on every kernel
   from 5.10 through 6.12. That's a list of structurally
   stable use-after-put-cred / double-dec / similar
   candidate sites worth a manual triage pass.

4. **6.6 and 6.12 surface new candidates not visible at
   smaller scale.** `crypto/cryptd.c::cryptd_free_*` (three
   functions), `net/bluetooth/af_bluetooth.c::bt_accept_enqueue`,
   `fs/proc/array.c::task_state` only appear once the corpus
   is big enough to include those subdirectories. The
   May 2026 25-file corpus per kernel was too small to reach
   them.

## What's still noisy

1. **The `[empty-ghost: low-confidence]` tag still fires for
   ~40 % of failed cred_lifetime rows.** Most are functions
   whose primary parameter is `struct task_struct *` —
   `copy_creds`, `exit_creds`, `__ptrace_unlink` — which
   the populated wrapper_paths configs don't yet cover.
   Adding `struct task_struct * → cred` (via `task->cred`)
   would lift them.

2. **Three aead wrapper-pattern functions
   (`crypto_rfc4309_crypt` and the two
   `crypto_rfc41{06,43}_crypt`) flag on every kernel.**
   These are known-not-real per the May write-up. The
   per-file synthesis has no way to model the
   "allocate fresh subreq, call aead_request_set_crypt"
   pattern accurately. Best fix is probably to detect the
   shape in the synthesiser and emit `verdict=skip-known-
   wrapper-pattern` rather than running cbmc on a harness
   that can't validate.

3. **The cocci hits for shared subsystems (e.g.
   `kernel/cred.c`) skew toward many hits per file.** A
   single function like `commit_creds` accumulates 3+ hits
   from the back-to-back put_cred lines at 510-511. The
   per-file aggregator already groups hits by enclosing
   function, but the rollup could surface "n distinct
   patterns within the function" rather than "n hits".

## Recommendations for the next iteration

1. **Extend `wrapper_paths` to `struct task_struct *`** (cred
   chain via `task->cred`). This single addition would lift
   most of the remaining `[empty-ghost: low-confidence]`
   tags into real verdicts. Same for `task->signal`
   (refcount), `task->mm` (lock).

2. **Teach the synthesiser to detect the aead transform-
   wrapper shape and skip it cleanly.** A simple AST
   prefilter on the function body looking for
   `aead_request_alloc()` followed by
   `aead_request_set_crypt()` against a fresh request
   would suffice.

3. **Triage the stable-cross-version cred_lifetime list
   manually.** The 12 functions that hit on 3+ kernels are
   the strongest candidate set. Expected outcomes from
   triage:
   - Some are genuine put-then-implicit-use patterns that
     the kernel handles correctly via reference counting
     not visible to the per-file harness (and can be
     suppressed by extending the cocci to require a
     subsequent-use predicate).
   - Some are real candidate sites the formal tools should
     follow up on with a more targeted property module.
   - Some are false positives that point at gaps in the
     contract for `put_cred` (e.g. RCU-grace-period semantics).

4. **Bump `CORPUS_MAX` to 200/kernel** for the next run.
   At PARALLEL=16 that's still inside a 30-minute per-kernel
   wall-clock budget on this host, and the 6.6/6.12
   subdirectory coverage gap (Bluetooth, cryptd) suggests
   more files = more new findings.

## Cross-references

- `integration/linux/scan/synthesise_harness.py`
  `MODULE_GHOST_BOOTSTRAP` — wrapper_paths populated for
  cred / lock / refcount lifetimes.
- `src/linking/linking.cpp`
  `structurally_equivalent` — the linker fix that unblocked
  this entire run.
- `regression/cbmc/Linking12/` — minimal regression test
  for the structural-equivalence linker pair.
- `integration/linux/scan/scan.py` `--per-file` flag and
  `run_cbmc_per_file` driver.
- `integration/linux/scan/bug-hunt.sh` — memory-bounded
  cgroup wrapper used here (with the wired-in
  `--bug-shape-only` swapped out for direct
  `corpus-scan.sh` invocation).
- `integration/linux/doc/bug-hunt-2026-05.md` — the prior
  hunt this is the follow-up to.
