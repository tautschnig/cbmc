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
- [`aead_kernel_stubs.c`](aead_kernel_stubs.c) — havocing bodies
  for `af_alg_wait_for_data`, `af_alg_alloc_areq`, `af_alg_get_rsgl`,
  `af_alg_count_tsgl`, `sock_kmalloc`, `af_alg_pull_tsgl`,
  `crypto_aead_copy_sgl`, `crypto_aead_{auth,req,iv}size`,
  `lock_sock_nested`, `release_sock`, `msg_data_left`,
  `aead_sufficient_data`, plus minor helpers.
- [`aead_kernel_harness.c`](aead_kernel_harness.c) — harness
  allocating 4 KiB each for `struct socket` and `struct msghdr`,
  invoking `_aead_recvmsg`.

Validated end-to-end against Linux 5.10 `crypto/algif_aead.c`:
cbmc terminates with `cbmc_status: "successful"` in seconds.
Caveat (LIM-009): the SUCCESSFUL verdict is vacuous because
`af_alg_get_rsgl` / `af_alg_pull_tsgl` are no-op stubs.  Soundly
refuting the Copy Fail pattern on `crypto/algif_aead.c` requires
richer stubs that materialise scatterlist entries with concrete
provenance; see LIM-009 in `../../CBMC_LIMITATIONS.md` for the
next-step plan.  The kernel-layout-shaped regression
(`../../cve-2026-31431/harness_kernel.c`) already demonstrates the
precise behaviour on hand-written kernel-shaped code.

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
