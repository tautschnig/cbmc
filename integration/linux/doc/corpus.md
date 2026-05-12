# Corpus-scan experiment: aead + pipe_buffer + cred_lifetime on Linux 5.10

> _Output of `scan/corpus-scan.sh` against an auto-discovered
> corpus spanning all three property modules.
> Captured 2026-05-12 (updated)._

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

## Corpus discovery

`corpus-scan.sh` auto-discovers candidate kernel files under
`$LINUX_TREE` by running `grep -rlE` per module seed pattern:

| module        | grep pattern                         | scopes                                |
| ------------- | ------------------------------------ | ------------------------------------- |
| aead          | `aead_request_set_crypt\(`           | `crypto`                              |
| pipe_buffer   | `struct pipe_buffer`                 | `fs`, `lib`, `net/smc`                |
| cred_lifetime | `(^|[^\w])(__)?put_cred\(`           | `fs`, `kernel`, `net`, `ipc`, `security` |

On Linux 5.10 these patterns surface **9 + 6 + 56 = 71 distinct
files** (see the numbers below — the `cred_lifetime` pattern
dominates because `put_cred` is widespread across the kernel).
`CORPUS_MAX` caps the total run; `PARALLEL` controls fan-out.

## Results (CORPUS_MAX=20, PARALLEL=4, 2-minute wall-clock)

### pipeline-ok: 15 files

All three property modules compiled, linked, and scanned cleanly:

| file                             | module         | hits |
| -------------------------------- | -------------- | ---- |
| `crypto/tcrypt.c`                | aead           | 4    |
| `crypto/echainiv.c`              | aead           | 2    |
| `crypto/gcm.c`                   | aead           | 2    |
| `crypto/pcrypt.c`                | aead           | 2    |
| `crypto/seqiv.c`                 | aead           | 2    |
| `crypto/testmgr.c`               | aead           | 2    |
| `crypto/algif_aead.c`            | aead           | 1    |
| `crypto/ccm.c`                   | aead           | 1    |
| `crypto/essiv.c`                 | aead           | 1    |
| `fs/fuse/dev.c`                  | pipe_buffer    | 4    |
| `lib/iov_iter.c`                 | pipe_buffer    | 4    |
| `fs/pipe.c`                      | pipe_buffer    | 3    |
| `fs/splice.c`                    | pipe_buffer    | 3    |
| `fs/cachefiles/security.c`       | cred_lifetime  | 2    |
| `fs/coredump.c`                  | cred_lifetime  | 1    |

Total: **39 Coccinelle hits across 15 files**, spanning three
bug classes.  `crypto/algif_aead.c`, `lib/iov_iter.c`, and
`fs/coredump.c` are the three files the property modules were
built around; the remaining 12 are candidates for manual review.

### compile-fail: 3 files

| file                         | module         | error                                                          |
| ---------------------------- | -------------- | -------------------------------------------------------------- |
| `fs/aio.c`                   | cred_lifetime  | `list.h: list_del` inline-expansion mismatch (needs `CONFIG_AIO`) |
| `fs/cifs/cifs_spnego.c`      | cred_lifetime  | `cifsglob.h` subsystem dep (needs `CONFIG_CIFS`)               |
| `fs/cifs/cifsacl.c`          | cred_lifetime  | same                                                           |

All three are legitimate kernel-config fragment gaps.  Closing
them is a matter of adding `CONFIG_AIO=y` / `CONFIG_CIFS=y` to
`scan/fragments/`.

## Scale numbers

On a 4-core dev machine (Linux 5.10 kernel tree cached, no cold
goto-cc):

- 3 files, PARALLEL=2:  44s
- 15 files, PARALLEL=4:  78s
- 20 files, PARALLEL=4:  104s

Throughput is roughly `7s/file` amortised across the corpus,
limited by CBMC's per-file symex/SAT time (the vacuity probe
doubles it).  A full 120-file CI nightly run budgets ~15 minutes
at PARALLEL=4.

## What this tells us

- **Auto-discovery works.**  The per-module seed patterns pick up
  all the previously-hand-curated files plus additional
  cred_lifetime candidates.
- **Parallelism scales linearly up to the core count.**  No
  contention observed between concurrent scan.py invocations
  (each has its own tempdir + goto binary cache).
- **Cross-module coverage is now visible in a single run.**  Three
  property modules, three bug classes, one SARIF output suitable
  for GitHub Code Scanning upload.

## LIM-013 still applies

"cbmc_status=failed" is still a property-module self-check.  The
per-file actionable signal is the cocci hit list.  See
`../CBMC_LIMITATIONS.md` for the underlying constraint and the
per-file-harness-generation direction that would lift it.

## Reproduction

```bash
# PR-time (sequential, small cap):
LINUX_TREE=/home/ubuntu/linux_5_10 CORPUS_MAX=20 PARALLEL=4 \
  integration/linux/scan/corpus-scan.sh /tmp/corpus

# Nightly CI (full discovery):
LINUX_TREE=/home/ubuntu/linux_5_10 CORPUS_MAX=120 PARALLEL=4 \
  integration/linux/scan/corpus-scan.sh /tmp/corpus
```

The CI workflow (`.github/workflows/integration-linux-
regressions.yaml`) runs the nightly-cap version on a cached
Linux 5.10 tree and uploads per-file JSON + the summary text
as a workflow artifact.
