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
   driver runs CBMC directly against a property-module link.
   Otherwise — real kernel source — the driver picks up the
   module's kernel adapter from [`adapters/`](adapters/) and links
   the adapter alongside the kernel goto binary and
   `page_provenance/page_provenance.c`.
3. **CBMC run.**  Compile + `goto-instrument
   --replace-call-with-contract <annotated_fn>` + `cbmc`.  The
   output is parsed for `VERIFICATION {SUCCESSFUL,FAILED}` plus any
   named `[…]: FAILURE` lines, and a per-module status is recorded:
   `successful`, `failed`, `timeout` (LIM-006 on real kernel today),
   or `error`.

The JSON report field `cbmc_status` carries one of those values
verbatim.

## Status on real kernel source

As of M4b: the end-to-end pipeline produces the correct goto-level
transformation on `crypto/algif_aead.c` — the contract ASSERT lands
at the expected `aead_request_set_crypt` call site inside
`_aead_recvmsg`.  The subsequent `cbmc` run on unstubbed
`_aead_recvmsg` hits the state-explosion case documented in
[LIM-006](../CBMC_LIMITATIONS.md); `scan.py` reports
`cbmc_status: "timeout"`.

Making real-kernel `cbmc_status` flip to `failed` or `successful`
needs aggressive stubbing of the kernel helpers
`_aead_recvmsg` calls transitively — `af_alg_wait_for_data`,
`af_alg_alloc_areq`, `af_alg_get_rsgl`, `af_alg_count_tsgl`,
`sock_kmalloc`, `crypto_aead_copy_sgl`, `af_alg_pull_tsgl`.  That's
the next milestone (M4c).

## M4a vs. M4b vs. M4c scope

- **M4a (landed earlier).**  Prefilter automation, CBMC run on
  property-module-native tests, JSON report.
- **M4b (this directory today).**  Kernel adapter under
  [`adapters/`](adapters/), `scan.py` routes real kernel source
  through the adapter, the goto-level contract substitution is
  verified on `crypto/algif_aead.c`.  Remaining outcome on that file
  is `timeout` (LIM-006).
- **M4c (next).**  Aggressive stubbing of kernel helpers so the cbmc
  run on real `_aead_recvmsg` either concludes `failed` (with a
  reproducible trace of the Copy Fail precondition violation) or
  `successful`.  SARIF report emission is supported directly by
  CBMC's `--sarif-result` flag (merged via `scan.py --sarif`)
  and by `scan.py --cocci-sarif` for GitHub Code Scanning
  integration (see the `Running scan.py in CI` section below).

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

### GitHub Code Scanning upload

For integration with the GitHub Code Scanning tab, pass
`--cocci-sarif` and upload the emitted file via
`github/codeql-action/upload-sarif@v3`:

```sh
scan.py $files \
    --cocci-sarif $RUNNER_TEMP/cbmc-linux-scan.sarif \
    --cocci-sarif-repo-root $LINUX_TREE

# in a subsequent step
- uses: github/codeql-action/upload-sarif@v3
  with:
    sarif_file: ${{ runner.temp }}/cbmc-linux-scan.sarif
    category: cbmc-linux-scan
    checkout_path: $LINUX_TREE
```

`--cocci-sarif` anchors each result at the kernel-source
file:line the Coccinelle prefilter reported, which is what
`github/codeql-action/upload-sarif` expects for a useful Code
Scanning experience.  Do NOT gate the PR on these results —
they are advisory candidates-for-review, not confirmed bugs
(see LIM-013 in `../CBMC_LIMITATIONS.md`).  Use `scan/run.sh`
cases 1–5 as the hard regression gate.

`.github/workflows/integration-linux-regressions.yaml` has a
working end-to-end example of this flow.

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
