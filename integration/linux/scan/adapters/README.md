# `scan/adapters/`

Per-property-module adapter sources that bridge a module's CBMC
contract onto the kernel's real API.  Each adapter is linked into
the pipeline `scan.py` drives against real kernel source:

```
goto-cc <kernel.gb> adapters/<module>_kernel_adapter.c \
       ../properties/page_provenance/page_provenance.c -o linked.gb
goto-instrument --replace-call-with-contract <annotated_fn> linked.gb trans.gb
cbmc --function <entry> trans.gb
```

## What an adapter provides

- **Opaque handles** for any kernel structs the contract references.
  goto-cc's linker unifies the forward declarations with the real
  kernel definitions present in the linked kernel goto binary, so the
  adapter never needs to know field offsets.
- **Re-implementations of any predicate walkers** that the property
  module's contracts call, but written against the kernel's in-memory
  layout rather than the property module's abstract representation.
  For example, the aead adapter re-implements
  `sgl_all_user_writable` against the kernel's bit-packed
  `scatterlist.page_link` encoding.
- **A declaration of the annotated function** with the full contract.
  goto-instrument attaches this contract to every call site of the
  function in the linked binary when `--replace-call-with-contract`
  runs.

## Today

- [`aead_kernel_adapter.c`](aead_kernel_adapter.c) — adapts the
  aead module.  Uses the kernel's `scatterlist.page_link` layout.
  Validated against Linux 5.10 `crypto/algif_aead.c`: the contract
  ASSERT lands at the expected call site inside `_aead_recvmsg`.

## Adding a new adapter

1. Create `scan/adapters/<module>_kernel_adapter.c` following the
   aead model.  Forward-declare the kernel structs you need as opaque
   (no field definitions unless you are re-implementing a
   layout-sensitive walker).
2. Register the adapter in `scan/scan.py`'s `KERNEL_ADAPTERS` dict.
3. Add an entry-point guess for your module in the
   `entry_candidates` map in `scan.py`'s `run_cbmc_kernel`.
4. Extend `scan/run.sh` with a test case against a real kernel
   file, checking for the expected `cbmc_status`.
