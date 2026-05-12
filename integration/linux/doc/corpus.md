# Corpus-scan experiment: aead + pipe_buffer on Linux 5.10

> _Output of `scan/corpus-scan.sh` against a curated 18-file corpus
> covering both property modules.  Captured 2026-05-12._

## What the experiment asked

Post-LIM-012, the scan's pipeline has three honest signals:

1. **Coccinelle prefilter hit count per file** — a textual match of
   the bug-class signature (e.g., `buf->page = X` without a
   preceding `buf->flags = 0` for pipe_buffer).
2. **Compile + link status** — does the kernel TU build under our
   allnoconfig + property-specific config fragments, and can
   goto-cc link it with the adapter, harness, and property module?
3. **Contract verdict on the direct-call harness** — does the
   pipeline fire the property module's contract on the synthetic
   vulnerable shape and pass it on the safe shape?

What the experiment **does not** ask — see LIM-013 below — is
"does file X contain the bug."  The direct-call harness is the
same across corpus files, so its verdict is a property-module
self-check, not a per-file signal.

## Corpus

Eighteen files, picked by `grep -l` on Linux 5.10 for the primary
kernel API each property module targets:

| module       | files                                                   |
| ------------ | ------------------------------------------------------- |
| aead         | `crypto/{algif_aead,ccm,echainiv,essiv,gcm,pcrypt,seqiv,tcrypt,testmgr}.c` |
| pipe_buffer  | `fs/{splice,pipe,fuse/dev,nfsd/vfs}.c`, `lib/iov_iter.c`, `net/smc/smc_rx.c`, `kernel/{trace/trace,relay,watch_queue}.c` |

## Results

### pipeline-ok: 13 files compiled, linked, and scanned cleanly

All produced the expected vulnerable-direction verdict
(`cbmc_status: failed`, precondition fires at the direct-call
harness's call site).  The per-file signal is the **Coccinelle hit
count**, not the cbmc verdict:

| file                                       | module      | hits |
| ------------------------------------------ | ----------- | ---- |
| `crypto/tcrypt.c`                          | aead        | 4    |
| `crypto/echainiv.c`                        | aead        | 2    |
| `crypto/gcm.c`                             | aead        | 2    |
| `crypto/pcrypt.c`                          | aead        | 2    |
| `crypto/seqiv.c`                           | aead        | 2    |
| `crypto/testmgr.c`                         | aead        | 2    |
| `crypto/algif_aead.c`                      | aead        | 1    |
| `crypto/ccm.c`                             | aead        | 1    |
| `crypto/essiv.c`                           | aead        | 1    |
| `fs/fuse/dev.c`                            | pipe_buffer | 4    |
| `lib/iov_iter.c`                           | pipe_buffer | 4    |
| `fs/pipe.c`                                | pipe_buffer | 3    |
| `fs/splice.c`                              | pipe_buffer | 3    |

Total: 31 cocci prefilter hits in 13 files.  `crypto/algif_aead.c`
and `lib/iov_iter.c` are the two the property modules were built
around.  The remaining 11 are candidates for manual review.

### compile-fail: 3 files could not be built

| file                   | error                                                              |
| ---------------------- | ------------------------------------------------------------------ |
| `kernel/relay.c`       | parse error on `int relay_prepare_cpu(...)` — config dependency    |
| `kernel/trace/trace.c` | `member 'trace_recursion' not found` — needs `CONFIG_TRACING`      |
| `kernel/watch_queue.c` | incomplete struct on left-hand side — needs `CONFIG_WATCH_QUEUE`   |

All three are legitimate build-config issues: our baseline is
`allnoconfig` plus small property-specific fragments, and these
subsystems are not enabled.  Closing out the compile-fails is a
config-fragment problem, not a CBMC problem.

### no-hits (silent pass)

Two files compiled but produced no cocci hits:

- `net/smc/smc_rx.c`
- `fs/nfsd/vfs.c`

Both use `struct pipe_buffer` but not in a way that matches the
`buf->page` or `buf->ops` assignment patterns the cocci rule
currently filters on.  Worth a look: is the cocci rule too
specific, or do these files legitimately not contain the bug
pattern?  Left as follow-up.

## What this tells us

- **Coccinelle prefilter works at scale.**  Thirteen files,
  thirty-one hits, all correctly typed to their module.  No
  false positives on files that have no `buf->page =` or
  `aead_request_set_crypt` at all.
- **The pipeline handles cross-TU scan cleanly.**  The same
  adapter, harness, stubs, and property module were reused across
  nine aead crypto/ files and four pipe_buffer fs/lib/ files
  without per-file tuning.
- **Three config-related build failures are honest limitations,
  not silent ones.**  They show up as `cbmc_status: error` with
  the compiler error captured in `cbmc_notes`.
- **LIM-013 (below) is real and known.**  Every pipeline-ok file
  reports `failed` because the direct-call harness is shared.  To
  get a per-file signal, we'd need per-file harness generation —
  left as a Phase 4 direction.

## LIM-013 — scan cannot currently give per-file bug verdicts

Filed alongside this experiment.  See
`integration/linux/CBMC_LIMITATIONS.md`.

## Reproduction

```bash
# ~30 minutes wall-clock on a 16 GB VM.
LINUX_TREE=/home/ubuntu/linux_5_10 \
  integration/linux/scan/corpus-scan.sh /tmp/corpus

# Raw JSON for each file is under /tmp/corpus/*.json; the
# compile logs are in /tmp/corpus/*.log.
```
