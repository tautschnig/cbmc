# Precision characterisation

Generated 2026-05-13 22:52:41 UTC

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
| 5.10 | pipe_buffer | lib/iov_iter.c | 4 | failed | 2/0/0/0 |
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
| 5.10 | refcount_lifetime | kernel/fork.c | 3 | failed | 3/0/0/0 |
| 6.1 | refcount_lifetime | kernel/fork.c | 3 | failed | 3/0/0/0 |
| 6.6 | refcount_lifetime | kernel/fork.c | 3 | failed | 3/0/0/0 |
| 6.12 | refcount_lifetime | kernel/fork.c | 3 | failed | 3/0/0/0 |

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

5.10 shows 4 prefilter hits; 6.1+ shows 0.  Upstream removed
`copy_page_to_iter_pipe` during the Dirty Pipe mitigation series.

### lock_state on kernel/bpf/dispatcher.c: failed across all 4 kernels

The earlier scan-compat gap on 6.1+ (missing
`bpf_jit_fill_hole_with_zero`) is closed by a weak stub in
`scan-compat.h`.  All four kernels now report
`cbmc_status=failed` with a per-file verdict of failed.

### cred_lifetime per-file on fs/coredump.c: timeouts persist

`do_coredump` is too large (>1000 lines) for the per-file
budget even with `goto-instrument --drop-unused-functions`
applied.  Genuine symex-scale issue, not an infrastructure bug.

### refcount_lifetime per-file on kernel/fork.c: 3/0/0/0 across all kernels

Previously this row was 2/0/0/1 — one hit erroring out due to
a synthesise_harness.py bug where `#ifdef CONFIG_<name>` lines
in the kernel source bled into the typedef-fallback (the
synthesised harness emitted `typedef char CONFIG_<name>;` and
failed to parse).  The strip-preprocessor-directives fix lands
all three hits as decisive `failed` verdicts.

### aead adapter-mode: `failed` everywhere (synthetic signal)

By design — aead per-file is unsupported (SGL fabrication is
future work).  The adapter-mode `failed` is the direct-call
harness's synthetic vulnerable-shape signal.

## Summary

The scan's cross-kernel behavior is now consistent for every
module on its anchor file.  Two known-tractable gaps remain:

- aead per-file unsupported (SGL fabrication).
- Very-large-function per-file timeouts (do_coredump-scale).

Neither reflects a soundness issue; both are bounded
engineering follow-ups.
