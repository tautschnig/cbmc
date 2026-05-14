# Bug hunt — first scaled run, May 2026

## Setup

- Scope: Linux 5.10 / 6.1 / 6.6 / 6.12 LTS kernels.
- CORPUS_MAX = 25 files per kernel (auto-discovered via the
  per-module seed patterns in `scan/corpus-scan.sh`).
- Mode: `scan/bug-hunt.sh` — `--per-file` synthesis enabled,
  per-cbmc RLIMIT_AS = 8 GiB, PARALLEL = 4, aggregate cap 25 %
  of host memory enforced via `systemd-run --user --scope`
  with `MemoryMax=188G` on a 755 GB host.
- Per-file timeout: 900 s.
- Total wall-clock: ~75 minutes for 4 kernels.

## Aggregate counts

| kernel | total cocci hits | per-file rows | failed | successful | noise | vacuous | error |
|--------|------------------|---------------|--------|------------|-------|---------|-------|
| 5.10   | 130              | 98            | 10     | 3          | 8     | 9       | 68    |
| 6.1    | 170              | 99            | 4      | 1          | 4     | 3       | 87    |
| 6.6    | 179              | 107           | 4      | 1          | 3     | 3       | 96    |
| 6.12   | 223              | 131           | 4      | 1          | 3     | 3       | 120   |

## Findings

### 1. The 'failed' candidates are dominated by harness artefacts

After triaging the 22 'failed' verdicts across all four kernels:

| pattern | examples | count (5.10) | reason |
|---------|----------|----|----------|
| aead transform-wrapper | `crypto_rfc4309_crypt`, `crypto_rfc4106_crypt`, `crypto_rfc4543_crypt` | 3 | function allocates a NEW `subreq` and calls `aead_request_set_crypt(subreq, rctx->src, rctx->dst, ...)` — ignores the harness's `req->dst` setup |
| empty-ghost no-param-match | `__pipe_unlock`, `nlmclnt_release_host`, `io_wq_destroy`, `file_free_rcu`, `nlm_destroy_host_locked` | 6 | enclosing function takes a wrapper struct (e.g. `struct nlm_host *`); the ghost-bootstrap config only matches the embedded primitive type (`struct mutex *`, `struct cred *`, …) directly, so the ghost is empty and contracts fire by default on lookups |
| param-bootstrapped but inner-field-used | `cachefiles_determine_cache_security` | 1 | parameter `cache_cred` IS bootstrapped, but `put_cred(cache->cache_cred)` reads a different cred (`cache->cache_cred`, an inner field of the cache struct) which isn't tracked |

After this triage, **0 of the 22 'failed' verdicts represent
plausible bug-class candidates**.  Every 'failed' is a known
harness-shape artefact.

### 2. The 'error' bucket is dominated by compile-failure noise

Across all four kernels, ~80 % of per-file rows fall into the
`error` bucket.  The vast majority are pre-existing
compile-failure issues with the kernel TU under our
scan-compat fragments — these are scan-compat gaps documented
in `CBMC_LIMITATIONS.md`, not bug-hunt findings.

### 3. The 'successful' / 'noise' verdicts are useful negative signal

Across the 4 kernels, 6 'successful' + 18 'noise' verdicts
mean: the synthesised harness reached a contract-clause
check, the contract HELD, and only CBMC built-in property
checks fired (or none did).  These are the cases where the
scan's per-file pipeline confirms the code path is
contract-compliant.

## What this hunt taught us

1. **The per-file harness's empty-ghost case dominates the
   noise floor**.  Six of ten 'failed' verdicts on Linux 5.10
   come from functions whose parameter types don't match any
   `MODULE_GHOST_BOOTSTRAP` entry directly.  The harness's
   ghost is empty, the contract precondition lookups return
   the safe default (not-live), and the contract fires on
   every reachable call site.  A meaningful bug hunt requires
   richer ghost-bootstrap detection: e.g. follow pointer
   chains through wrapper structs to find the embedded
   `struct cred *` / `struct mutex *` / `refcount_t *` and
   bootstrap them.

2. **The aead transform-wrapper pattern needs special
   handling**.  Functions like `crypto_rfc4309_crypt` follow
   a stereotyped 'wrap, set new SGL, call set_crypt' shape.
   The harness can't predict the runtime SGL shape; aead
   per-file synthesis is fundamentally limited on these.

3. **Compile failures dwarf scan signal at corpus scale**.
   The current scan-compat fragments cover the first-order
   issues but plenty of files (including all of fs/nfs/*
   under flexfilelayout, fs/lockd/*, etc.) still fail to
   compile under our flags.  Closing each compile failure
   would shrink the error bucket and let the per-file scan
   actually run on 100 % of cocci hits.

## Recommendations for the next hunt

1. **Improve ghost-bootstrap recall**: extend
   `MODULE_GHOST_BOOTSTRAP` to recognise common wrapper
   structs (e.g. `struct nlm_host *` carries a
   `host->cred`; `struct cachefiles_cache *` carries a
   `cache->cache_cred`).  This would lift many 'low-
   confidence' rows into 'high-confidence' verdicts.

2. **Detect transform-wrapper aead functions and skip them**:
   if the synthesised harness's enclosing function takes a
   `struct aead_request *` AND the body allocates a fresh
   subrequest before calling `aead_request_set_crypt`, the
   per-file synthesis can't validate the call.  Skip these
   rather than producing false-positive 'failed'.

3. **Close more scan-compat gaps**: each compile-failure
   closed lifts dozens of files into the per-file rollup.

4. **Per-file synthesis for functions without a
   bug-class-matching param should produce 'vacuous'
   rather than 'failed' UNLESS the body's call pattern
   includes the bug-class shape**.  This requires
   AST-level analysis of the function body (e.g. detect
   back-to-back put_cred or unlock-without-lock) before
   running cbmc.

The bug-hunt INFRASTRUCTURE is solid — memory caps work
correctly (peak observed: ~32 GB / 188 GB cap), per-file
verdicts capture confidence, the 4-kernel matrix runs in
~75 minutes — but the synthesised harness's signal-to-noise
ratio at this stage is too low to surface real bugs without
additional precision work.
