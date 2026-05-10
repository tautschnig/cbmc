# `scan/` — pipeline driver and real-kernel compilation helpers

Seed directory for the PR-scan driver described in
[`../DESIGN.md §9`](../DESIGN.md).  Currently contains the helpers
for turning real kernel source into goto-cc goto binaries.  The full
pipeline driver (Coccinelle prefilter + harness generator + cbmc
runner + report aggregator) lands in milestone M4.

## What's here today

- `configure.sh` — configure a kernel tree for scanning from
  `allnoconfig` + one or more config fragments.  Merges the
  fragments via the kernel's own
  `scripts/kconfig/merge_config.sh`, runs `make olddefconfig`, and
  tries `make prepare scripts`, falling back gracefully on hosts
  that hit [LIM-005](../CBMC_LIMITATIONS.md).
- `fragments/` — the config fragments library.  See its
  [`README.md`](fragments/README.md) for the available fragments
  and how to add more.  The `baseline.config` + `crypto-aead.config`
  pair is enough to compile `crypto/algif_aead.c`.
- `compile_file.sh` — build a single kernel `.c` file into a
  goto-cc goto binary without driving the kernel's full `make`
  system.  Takes a pre-configured kernel tree plus a source path
  and produces a goto binary ready for `goto-instrument` and
  `cbmc`.  Validated against `crypto/algif_aead.c` from Linux 5.10.

## End-to-end today

```sh
# One-time: configure the kernel tree for the subsystems you want to scan.
scan/configure.sh /path/to/linux \
    scan/fragments/baseline.config \
    scan/fragments/crypto-aead.config

# Per-file: produce a goto binary.
scan/compile_file.sh /path/to/linux crypto/algif_aead.c /tmp/algif.gb

# Inspect.
build/bin/goto-instrument --show-goto-functions /tmp/algif.gb | less
```

## What's planned (M4)

The compile pipeline above is in place, but the end-to-end link-up
with the property modules is still to be automated.  The open items,
in the order they need to be addressed:

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

## Limitations and caveats

- `compile_file.sh`'s flag set is tuned for `x86_64` plus a broadly
  sensible kernel config.  Files that require per-subdir `Kbuild`
  flags may need ad-hoc extensions; check the kernel's own `.o.cmd`
  cache after a normal `gcc` build for ground truth.
- No support for assembly sources, no cross-compilation.  Punted to
  M4.
- The build does not run the kernel's own objtool or modpost stages;
  those are irrelevant for property-module verification.
