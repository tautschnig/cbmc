# Linux kernel integration

CBMC-based pre-merge static analysis of the Linux kernel.  See
[`DESIGN.md`](DESIGN.md) for the architecture and milestones.

## Layout

- [`DESIGN.md`](DESIGN.md) — architecture, milestones, conventions.
- [`compile_linux.sh`](compile_linux.sh) — clones Linux v5.10 and
  re-compiles it with `goto-cc` via `one-line-scan`.  Produces goto
  binaries embedded in kernel ELF objects.  Used as the source of
  real kernel code for the analysis tool.
- `properties/` — property-module library (one subdirectory per
  kernel primitive; contracts, reference implementations, and
  Coccinelle prefilters).  Added in milestone M2 onwards.
- `cve-*/` — per-CVE regression harnesses that exercise one or more
  property modules against the source shape of a historical bug.
  First entry: [`cve-2026-31431/`](cve-2026-31431/) ("Copy Fail").

## Running the CVE regressions

Each `cve-*/` directory carries its own `run.sh` that produces a
structured pass/fail report.  Example:

```sh
./cve-2026-31431/run.sh
```

The scripts assume `cbmc` is at `build/bin/cbmc` relative to the
repository root; set `$CBMC` to override.

## Status

This tree is under active development.  Each milestone lands as a
separate, reviewable commit sequence.  See `DESIGN.md §12` for the
current milestone list and progress.
