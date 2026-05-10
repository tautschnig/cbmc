# `scan/` — PR-scan driver and real-kernel compilation helpers

Implementation of the pipeline described in
[`../DESIGN.md §9`](../DESIGN.md).  Milestone M4a
(prefilter + driver + JSON report) is in place; M4b (real-kernel
link-up via a source-level adapter) is next.

## Components

- [`scan.py`](scan.py) — the driver.  Takes one or more C source
  paths, runs each property module's Coccinelle prefilter over
  them, and for every prefilter hit runs a CBMC scan with
  `--replace-call-with-contract` applied for the module's annotated
  primitives.  Emits a per-file summary on stdout and, with
  `--json`, a structured report.  Exit code 1 iff any file had a
  `cbmc_status == "failed"`.
- [`run.sh`](run.sh) — regression driver for `scan.py`; runs it
  against a property-module-native CVE harness and (if the kernel
  tree is present at `$LINUX_TREE`) against real
  `crypto/algif_aead.c`, checking that the expected outcomes are
  produced.
- [`compile_file.sh`](compile_file.sh) — build a single kernel `.c`
  file into a goto-cc goto binary without driving the kernel's make
  system.
- [`configure.sh`](configure.sh) — configure a kernel tree for
  scanning from `allnoconfig` + one or more config fragments.
- [`fragments/`](fragments/) — config fragment library for
  `configure.sh`.

## End-to-end usage today

```sh
# One-time: configure a kernel tree for the subsystems you want to scan.
scan/configure.sh /path/to/linux \
    scan/fragments/baseline.config \
    scan/fragments/crypto-aead.config

# Per-file or per-PR: drive scan.py.  --json emits a structured report.
scan/scan.py /path/to/linux/crypto/algif_aead.c --json report.json
```

## What scan.py does

For each input file, and for each property module in
`../properties/<name>/` that ships a `.cocci` file:

1. **Prefilter.**  Run `spatch --sp-file properties/<name>/<name>.cocci`
   against the file.  Collect line-level hits.
2. **Triage.**  If the file includes one of the property module
   headers (`page_provenance.h`, `scatterlist.h`, `aead.h`) the
   driver has enough information to run CBMC directly.  Otherwise —
   real kernel source — the driver reports
   `cbmc_status: "adapter-needed"`.  This is the current M4a /
   M4b boundary; see below.
3. **CBMC run** (for scannable cases).  Compile the input together
   with all property module sources via `goto-cc`, apply
   `goto-instrument --replace-call-with-contract` for each of the
   module's annotated functions, invoke `cbmc`, parse the output for
   `VERIFICATION {SUCCESSFUL,FAILED}` plus any named `[…]: FAILURE`
   assertions, and record the result in the report.

The summary printed on stdout is a two-level list (file → module);
the JSON is a strict serialisation of the internal report objects.

## M4a vs. M4b scope

M4a (this directory today) covers:

- Automation of the prefilter + CBMC chain for property-module-native
  input (e.g. CVE regression harnesses that `#include` the module
  headers).  Demonstrated by `run.sh` on
  `../properties/aead/test_copyfail.c`.
- Accurate reporting on real kernel input: the prefilter runs, the
  hit line is recorded, and the report is honest about why a full
  proof/refutation cannot yet be produced.
- Structured JSON output suitable for downstream tooling (CI, SARIF
  conversion, dashboards).

M4b (next session) will add:

- **Source-level kernel adapter.** Use `-include` ordering plus
  macros to intercept each module's annotated `static inline`
  kernel function at preprocessing time, forwarding to a
  contract-carrying wrapper that lives alongside the property
  module.  This gets around GCC inlining happening before goto-cc
  can see the call sites (see LIM-004 in
  [`../CBMC_LIMITATIONS.md`](../CBMC_LIMITATIONS.md), updated in
  M3-stretch).
- **Aggressive stubbing** of kernel helpers not directly relevant
  to the property being checked, to keep `cbmc` runs tractable on
  real kernel functions.  See LIM-006.
- **Multi-function entry-point discovery** and per-function CBMC
  budgets, so that scan.py can report per-function results within
  a file.

When M4b lands, the only change to M4a's API will be that files
previously reported as `adapter-needed` will instead return
`successful` or `failed` with a full trace.

## Coccinelle rule style

Prefilter rules live in `../properties/<name>/<name>.cocci`.  They
are deliberately coarse (high recall, modest precision): a hit says
"a call site of the annotated primitive lives here; CBMC is warranted";
a miss says "no call; skip CBMC for this (file, module) pair."  CBMC
is the authoritative source of truth for whether a hit is actually
a problem.

## Running scan.py in CI

Per-PR:

```sh
git diff --name-only $BASE..HEAD | grep '\.c$' | \
    xargs -r scan/scan.py --json scan-report.json
```

`--json` produces a machine-readable report with a stable schema
(`schema: "cbmc-linux-scan.v1"`) that downstream consumers can
convert to SARIF or dashboard-friendly formats.

## Limitations and caveats

- `compile_file.sh`'s flag set is tuned for `x86_64` plus a broadly
  sensible kernel config.  Files that require per-subdir `Kbuild`
  flags may need ad-hoc extensions; check the kernel's own
  `.o.cmd` cache after a normal `gcc` build for ground truth.
- The M4a driver trusts the file's own `#include` lines to decide
  whether to run CBMC directly; files that pull in the property
  modules transitively via a shared header would need the
  `file_uses_property_modules` heuristic in `scan.py` broadened.
- No support for assembly sources, no cross-compilation.
