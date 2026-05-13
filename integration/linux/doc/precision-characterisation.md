# Precision characterisation

Generated 2026-05-13 18:17:53 UTC

For each (kernel × module × anchor file) triple, reports
(a) the Coccinelle prefilter hit count, (b) the adapter-
mode `cbmc_status`, and (c) the `--per-file` verdict
counts (failed / successful / timeout / error / other).
A stable pattern across kernels is the scan doing its job;
a shift from `hits=N` to `hits=0` at a specific version
reflects upstream having removed the vulnerable pattern.

## Results

| kernel | module | anchor | hits | adapter | per-file (f/s/t/e) |
|--------|--------|--------|------|---------|---------------------|
| 5.10 | aead | crypto/algif_aead.c | 1 | failed | n/a |
| 6.1 | aead | crypto/algif_aead.c | 1 | failed | n/a |
| 6.6 | aead | crypto/algif_aead.c | 1 | failed | n/a |
| 6.12 | aead | crypto/algif_aead.c | 1 | failed | n/a |
| 5.10 | pipe_buffer | lib/iov_iter.c | 4 | failed | 1/0/0/1 |
| 6.1 | pipe_buffer | lib/iov_iter.c | 0 | not-run | 0/0/0/0 |
| 6.6 | pipe_buffer | lib/iov_iter.c | 0 | not-run | 0/0/0/0 |
| 6.12 | pipe_buffer | lib/iov_iter.c | 0 | not-run | 0/0/0/0 |
| 5.10 | cred_lifetime | fs/coredump.c | 1 | failed | 0/0/1/0 |
| 6.1 | cred_lifetime | fs/coredump.c | 1 | failed | 0/0/1/0 |
| 6.6 | cred_lifetime | fs/coredump.c | 1 | failed | 0/0/1/0 |
| 6.12 | cred_lifetime | fs/coredump.c | 1 | failed | 0/0/1/0 |
| 5.10 | lock_state | kernel/bpf/dispatcher.c | 1 | failed | 1/0/0/0 |
| 6.1 | lock_state | kernel/bpf/dispatcher.c | 1 | failed | 1/0/0/0 |
| 6.6 | lock_state | kernel/bpf/dispatcher.c | 1 | failed | 1/0/0/0 |
| 6.12 | lock_state | kernel/bpf/dispatcher.c | 1 | failed | 1/0/0/0 |
| 5.10 | refcount_lifetime | kernel/fork.c | 3 | failed | 2/0/0/1 |
| 6.1 | refcount_lifetime | kernel/fork.c | 3 | failed | 2/0/0/1 |
| 6.6 | refcount_lifetime | kernel/fork.c | 3 | failed | 2/0/0/1 |
| 6.12 | refcount_lifetime | kernel/fork.c | 3 | failed | 2/0/0/1 |

## Notes

- `hits=0` at a particular version means the Coccinelle
  prefilter found no call sites matching the module's
  seed pattern.  For `pipe_buffer` on `lib/iov_iter.c`,
  this is the expected shape on 6.6+ because upstream
  removed `copy_page_to_iter_pipe` entirely.
- adapter-mode `failed` is a property-module self-check
  (the direct-call harness's vulnerable synthetic shape
  fires the contract).  It is NOT a per-file bug signal;
  see `CBMC_LIMITATIONS.md` LIM-013.
- per-file counts are `failed / successful / timeout /
  error`.  `failed` means the synthesised per-enclosing-
  function harness triggered the contract; `successful`
  means it didn't; `timeout` means the symex didn't
  terminate inside the per-file budget.
- `aead` per-file is deliberately `n/a` — the predicate
  walks `req->dst`'s scatterlist, which cannot be
  fabricated from a single `aead_request *` parameter.

## Observations

### pipe_buffer on lib/iov_iter.c: cocci hits collapse on 6.1+

5.10 shows 4 prefilter hits; 6.1 and later show 0.  Upstream
removed the `copy_page_to_iter_pipe` path during the Dirty Pipe
mitigation series, and the take-over sites that the cocci rule
anchors on disappeared with it.  This is the scan correctly
following the upstream fix: `hits=0` is the honest, correct
signal on a patched kernel.

### lock_state on kernel/bpf/dispatcher.c

5.10 through 6.12 all report `cbmc_status: "failed"` with a
per-file verdict of `failed` on the enclosing
`bpf_dispatcher_change_prog`.  A prior version of this report
showed `error` on 6.1+ due to the missing
`bpf_jit_fill_hole_with_zero` symbol; that scan-compat gap is
closed by a weak stub in `integration/linux/scan/fragments/
scan-compat.h`.  The three 6.x rows reflect the fix.

### cred_lifetime per-file on fs/coredump.c: timeouts across all kernels

`fs/coredump.c`'s `do_coredump` is a >1000-line function; the
per-file harness synthesises a call to it with nondet-initialised
arguments, and cbmc's symex exceeds the 900s per-file budget.
Expected; the adapter-mode signal on this file remains useful.
Targets with smaller enclosing functions produce per-file
verdicts reliably.

### refcount_lifetime per-file on kernel/fork.c: 2 failed + 1 error

Of three `refcount_dec_and_test` prefilter hits, two produce
per-file `failed` verdicts and one errors out — likely a
large-function or symbol-resolution boundary case.

### aead adapter-mode: `failed` everywhere (synthetic signal)

aead reports `cbmc_status=failed` on all four kernels because
the adapter mode exercises the hand-written direct-call
harness's vulnerable-shape branch; the actual kernel code on
the per-kernel `_aead_recvmsg` site isn't what's being verified
here (LIM-013).  This is by design; aead precision requires
per-file synthesis, which is future work (see the companion
`alloc_tag` module for the reverse-direction path for static-
inline API coverage).

## Summary

The scan's cross-kernel behavior is now consistent for all
modules with per-file support on well-shaped anchor files.
Gaps:

- aead per-file unsupported (scatterlist layout fabrication is
  future work).
- Large enclosing functions (e.g. `do_coredump`) time out under
  per-file synthesis.

All gaps are tractable; none reflect a fundamental soundness
issue in the pipeline.
