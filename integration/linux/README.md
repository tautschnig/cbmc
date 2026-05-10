# Linux kernel integration

CBMC-based pre-merge static analysis of the Linux kernel.  See
[`DESIGN.md`](DESIGN.md) for the architecture and milestones.

## Layout

- [`DESIGN.md`](DESIGN.md) — architecture, milestones, conventions.
- [`CBMC_LIMITATIONS.md`](CBMC_LIMITATIONS.md) — running log of CBMC
  constraints we have hit while building the tool.  Append-only.
- [`compile_linux.sh`](compile_linux.sh) — clones Linux v5.10 and
  re-compiles it with `goto-cc` via `one-line-scan`.  Produces goto
  binaries embedded in kernel ELF objects; used as the source of
  real kernel code for the analysis tool.
- [`properties/`](properties/) — property-module library (one
  subdirectory per kernel primitive: contracts, reference
  implementations, Coccinelle prefilters, tests).  See
  [`properties/README.md`](properties/README.md) for the module
  convention.
- [`scan/`](scan/) — seed for the PR-scan driver (milestone M4).
  Today contains only `compile_file.sh`, a helper that compiles a
  single kernel `.c` file to a goto-cc binary outside the kernel's
  own make system.
- `cve-*/` — per-CVE regression harnesses that exercise one or more
  property modules against the source shape of a historical bug.
  First entry: [`cve-2026-31431/`](cve-2026-31431/) ("Copy Fail").

## Running the regressions

Each `cve-*/` and `properties/<module>/` directory carries its own
`run.sh` that produces a structured pass/fail report.  Examples:

```sh
# Fast CVE regression (abstract model, ~20 s)
./cve-2026-31431/run.sh

# Per-module property tests
./properties/page_provenance/run.sh
./properties/scatterlist/run.sh
./properties/aead/run.sh
```

The scripts assume `cbmc`, `goto-cc` and `goto-instrument` are at
`build/bin/` relative to the repository root; set `$CBMC`,
`$GOTOCC`, `$GI` respectively to override.

## Status

This tree is under active development.  Each milestone lands as a
separate, reviewable commit sequence.  See
[`DESIGN.md §12`](DESIGN.md#12-milestones) for the milestone list
and [`CBMC_LIMITATIONS.md`](CBMC_LIMITATIONS.md) for the running
list of constraints we are accumulating alongside.

Completed through commit — see `git log -- integration/linux/`:

- M0: abstract-model regression for CVE-2026-31431.
- M1: prototype cleanup, top-level README.
- M2: property-module infrastructure + first module (`page_provenance`).
- M3 (core): two more property modules (`scatterlist`, `aead`);
  end-to-end contract-based Copy-Fail detection in
  `properties/aead/test_copyfail.c`.
- M3 (stretch): `scan/compile_file.sh` validated against real
  `crypto/algif_aead.c` from Linux 5.10; end-to-end link-up
  deferred to M4.

In progress / next: M4 (PR-scan driver + link-up of property
modules against real kernel goto binaries).
