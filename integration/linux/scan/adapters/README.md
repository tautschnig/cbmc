# `scan/adapters/`

Per-property-module adapter sources that bridge a module's CBMC
contract onto the kernel's real API.  For each module a complete
adapter is a set of three C files:

- `<module>_kernel_adapter.c` — contract-only declaration of the
  annotated kernel function, re-implementations of any contract
  predicates against the kernel's in-memory layout, and opaque
  forward declarations for the kernel types involved.
- `<module>_kernel_stubs.c` — havocing bodies for the kernel helpers
  the annotated function transitively calls.  `scan.py` links these
  so CBMC stops treating the helpers as fully undefined; the stubs
  may additionally tag ghost state (e.g. set page provenance after
  `af_alg_get_rsgl` materialises a user SGL).
- `<module>_kernel_harness.c` — a `main()` that
  `__CPROVER_allocate`s concrete storage for the opaque kernel types
  and calls the annotated-function's entry point.  Constrains
  initial-state geometry so cbmc's pointer analysis starts from a
  small set of distinct heap objects.

`scan.py` links all three alongside the compiled kernel goto binary
and `properties/page_provenance/page_provenance.c`, applies
`goto-instrument --replace-call-with-contract <hook>`, and runs
`cbmc --function main --unwind 2 --no-unwinding-assertions
--no-standard-checks`.

## Today

- [`aead_kernel_adapter.c`](aead_kernel_adapter.c) — adapts the
  aead module.  Uses the kernel's `scatterlist.page_link` layout.
- [`aead_kernel_adapter_probe.c`](aead_kernel_adapter_probe.c) —
  vacuity-probe variant of the adapter (contract has
  `__CPROVER_requires(0 == 1)`).  `scan.py` runs a probe scan
  against the linked binary to detect unreachable call sites.
- [`aead_kernel_direct_harness.c`](aead_kernel_direct_harness.c) —
  direct-call harness.  Builds a kernel-layout scatterlist
  explicitly (vulnerable or safe, selected by `-DFIXED`) and
  calls the contract target.  This supersedes the earlier
  through-`_aead_recvmsg` harness that LIM-012 showed was not
  soundly end-to-end.

(An earlier iteration shipped an `aead_kernel_stubs.c` that
provided havocing bodies for the kernel externs
`crypto/algif_aead.c` references.  Under the direct-call harness
the kernel TU's `_aead_recvmsg` body is never called from `main`,
so those stubs were dead weight whose kernel-header inclusions
produced cross-TU `static inline` conflicts on newer kernels —
see LIM-011, now RESOLVED.  The stubs file has been retired;
goto-cc resolves the remaining kernel externs as nondet-return
stubs automatically, and they are unreachable from `main`.)

Validated end-to-end against Linux 5.10 `crypto/algif_aead.c`:
- Default (`--direction=vuln`): `cbmc_status: "failed"`,
  `precondition.3` (sgl_all_user_writable) fires at the harness's
  call site.  The same kernel source is compiled and linked, so
  LIM-009's required-bodies guardrail continues to catch any
  regression in the static-symbol name resolution.
- Fix direction (`--direction=fix`): `cbmc_status: "successful"`,
  demonstrating the contract accepts a safe SGL shape on the
  same pipeline.  Closes LIM-010 / LIM-012.

### pipe_buffer (Dirty Pipe / CVE-2022-0847)- [`pipe_buffer_kernel_adapter.c`](pipe_buffer_kernel_adapter.c) —
  attaches `pipe_buf_merge_safe(buf) == 1` as a contract
  precondition on `pipe_buf_release` (static inline in
  `<linux/pipe_fs_i.h>`; contract declared on both the external
  name and the `__CPROVER_file_local_pipe_fs_i_h_pipe_buf_release`
  mangled name).
- [`pipe_buffer_kernel_adapter_probe.c`](pipe_buffer_kernel_adapter_probe.c) —
  trivially-false-precondition variant for the vacuity probe.
- [`pipe_buffer_kernel_direct_harness.c`](pipe_buffer_kernel_direct_harness.c) —
  direct-call harness.  Three-step shape: writer A populates with
  CAN_MERGE, slot is taken over (vulnerable: without flag reset;
  fixed with `-DFIXED`: with flag reset), then `pipe_buf_release`
  is called.  The contract fires in the vulnerable direction and
  passes in the fix direction.

Validated end-to-end against Linux 5.10 `lib/iov_iter.c`
(home of `copy_page_to_iter_pipe`, the original Dirty Pipe
bug site):
- Default: `cbmc_status: "failed"`,
  `pipe_buf_release.precondition.2` fires at the harness's
  `pipe_buf_release` call site.  Coccinelle prefilter surfaces
  four take-over sites in the file.
- `--direction=fix`: `cbmc_status: "successful"`.

### cred_lifetime (CVE-2026-23297 class)

- [`cred_kernel_adapter.c`](cred_kernel_adapter.c) — attaches
  `cred_live(c) == 1` as a contract precondition on `put_cred`
  (static inline in `<linux/cred.h>`; contract declared on both
  the external name and the
  `__CPROVER_file_local_cred_h_put_cred` mangled name).
- [`cred_kernel_adapter_probe.c`](cred_kernel_adapter_probe.c) —
  trivially-false-precondition variant for the vacuity probe.
- [`cred_kernel_direct_harness.c`](cred_kernel_direct_harness.c) —
  direct-call harness.  Builds a cred with usage=1 (vulnerable)
  or usage=2 (`-DFIXED`), then calls `put_cred` twice.  The
  vulnerable shape fires the `cred_live` precondition at the
  second put; the fixed shape passes.

Validated end-to-end against Linux 5.10 `fs/coredump.c`
(one of the many sites flagged by the cocci prefilter for
`put_cred` calls):
- Default: `cbmc_status: "failed"`, `put_cred.precondition.4`
  fires at the harness's second `put_cred` call.
- `--direction=fix`: `cbmc_status: "successful"`.

## Adding a new adapter

1. Create `scan/adapters/<module>_kernel_{adapter,stubs,harness}.c`
   following the aead model.  Forward-declare the kernel structs
   you need as opaque (no field definitions unless you are
   re-implementing a layout-sensitive walker).
2. Register the adapter in `scan/scan.py`'s `KERNEL_ADAPTERS` dict,
   listing adapter, stubs, harness, and `deps`.
3. Add a CVE regression under `integration/linux/cve-*` that
   exercises the module on both vulnerable and fixed shapes.
4. Extend `scan/run.sh` with a case against a real kernel file,
   checking for the expected `cbmc_status`.
