# `scan/` — pipeline driver and real-kernel compilation helpers

Seed directory for the PR-scan driver described in
[`../DESIGN.md §9`](../DESIGN.md).  Currently contains only the
minimal real-kernel compilation helper; the full pipeline lands in
milestone M4.

## What's here today

- `compile_file.sh` — build a single kernel `.c` file into a goto-cc
  goto binary without driving the kernel's `make` system.  Takes a
  pre-configured kernel tree plus a source path and produces a goto
  binary ready for `goto-instrument` and `cbmc`.  Validated against
  `crypto/algif_aead.c` from Linux 5.10.

## What's planned (M4)

The M3 stretch experiment showed we can compile real kernel source
with `goto-cc` (see `compile_file.sh`), but we have not yet
automated the end-to-end link-up with the property modules.  The
open items, in the order they need to be addressed:

1. **Strip the kernel's inlined `static inline` copies of annotated
   primitives** (e.g. `aead_request_set_crypt`,
   `aead_request_set_ad`, `aead_request_set_tfm`, the scatterlist
   helpers) from the compiled goto binary using
   `goto-instrument --remove-function-body <name>`.  See LIM-004 in
   [`../CBMC_LIMITATIONS.md`](../CBMC_LIMITATIONS.md).
2. **Link the stripped kernel binary against the property modules'
   reference implementations** with `goto-cc <stripped.gb>
   ../properties/*/*.c -o linked.gb`.  The kernel call sites now
   refer to the contract-carrying versions in `properties/aead/`,
   `properties/scatterlist/`, and `properties/page_provenance/`.
3. **Reconcile `struct scatterlist` layouts.** The property modules'
   definition uses explicit `chain` / `end` fields; the kernel bit-packs
   them into `page_link`.  Adapter approach is documented in
   [`../properties/scatterlist/README.md`](../properties/scatterlist/README.md)
   under "Representation."  The substitution path (property module's
   header wins via `-include` ordering) is expected to work for
   `crypto/algif_aead.c`; the translation path is the fallback.
4. **Apply the relevant contracts** via
   `goto-instrument --replace-call-with-contract
   aead_request_set_crypt --replace-call-with-contract sg_chain ...`
   and run `cbmc --function _aead_recvmsg`.  Per LIM-006, this
   requires aggressive stubbing of other kernel helpers to be
   tractable.
5. **Coccinelle prefilter.** Add `.cocci` rules (per module) that
   identify functions worth scanning, so the pipeline runs CBMC only
   on candidates rather than on every changed file.  Matches DESIGN
   §3 and §7.
6. **Report aggregator.** Structured JSON + human HTML summary of
   contract failures per file / function / module.  SARIF is the
   likely interchange format.

## Running `compile_file.sh` manually

```sh
# Prepare kernel source once (generated headers, scripts):
cd /path/to/linux
make olddefconfig
make prepare scripts     # may need workarounds per LIM-005

# Produce a goto binary of a single file:
/path/to/cbmc-github.git/integration/linux/scan/compile_file.sh \
    /path/to/linux crypto/algif_aead.c /tmp/algif.gb

# Inspect:
/path/to/cbmc-github.git/build/bin/goto-instrument \
    --show-goto-functions /tmp/algif.gb | less
```

## Limitations and caveats

- The flag set in `compile_file.sh` is tuned for `x86_64` plus
  `allnoconfig + CONFIG_KVM + CONFIG_CRYPTO_USER_API_AEAD`.  Files
  in other subsystems may need additional kernel flags or different
  `-include` ordering; check the kernel's own `.o.cmd` cache after
  a normal `gcc` build for ground truth.
- No support for assembly sources, no cross-compilation, no
  per-subsystem Kconfig detection.  All punted to M4.
- The build does not run the kernel's own objtool or modpost stages;
  those are irrelevant for property-module verification anyway.
