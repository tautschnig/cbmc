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

# --per-file mode: synthesise a per-enclosing-function harness per
# cocci hit instead of using the hand-written direct-call harness.
# Verdicts are reported per-hit in `report.json.files[].modules[].per_file`.
LINUX_TREE=/path/to/linux \
    scan/scan.py --per-file /path/to/linux/kernel/ptrace.c \
    --json ptrace-per-file.json
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

## --per-file mode

By default `scan.py` drives a hand-written direct-call harness
(one per property module, under `scan/adapters/`) that exercises
one kernel API with a curated set of input shapes.  This is the
fastest path and the verdict has the clearest signal, but it
only verifies the API, not the specific call sites the
Coccinelle prefilter flagged.

`scan.py --per-file` flips the pipeline: for each Coccinelle
prefilter hit, scan.py

1. determines the hit's enclosing function by regex-scanning the
   kernel source,
2. invokes `scan/synthesise_harness.py` to generate a harness
   that calls that enclosing function with nondet-initialised
   arguments and bootstraps the relevant ghost-state,
3. runs the full scan pipeline through `scan/scan-per-file.sh`
   (compile kernel TU + compile harness + link + `goto-instrument
   --replace-call-with-contract` + `cbmc --function
   <func>_per_file_harness`), and
4. records per-hit verdicts in the JSON report's
   `files[].modules[].per_file` field.

`cbmc_status` is aggregated across all per-file verdicts:
`failed` wins over `timeout`, which wins over `error`, which
wins over `successful`.  The regression test lives at
[`scan/test-per-file-mode.sh`](test-per-file-mode.sh).

Supported modules: `cred_lifetime`, `pipe_buffer`, `lock_state`,
`refcount_lifetime`, `aead`, and `alloc_tag`.  Extending to other
modules requires a small config block in
`scan/synthesise_harness.py` `MODULE_GHOST_BOOTSTRAP` plus a
default `contract_targets` list (`scan.py` picks these up from
its existing `CONTRACT_FUNCTIONS` dict automatically).

`aead` per-file uses a custom multi-statement bootstrap that
includes `<crypto/aead.h>`, allocates a 1-element scatterlist
on a page-aligned backing buffer, marks the page
`PAGE_USER_WRITABLE` in the page_provenance ghost, and assigns
`req->dst` to the SGL.  The synthesised harness then invokes
the enclosing function with the prepared request; if the
function reassigns `req->dst` before reaching the contracted
`aead_request_set_crypt` call, the verdict reflects the new
SGL's provenance.  See `MODULE_GHOST_BOOTSTRAP['aead']` in
`scan/synthesise_harness.py` for the template.

Per-file mode is opt-in because it is substantially slower (a
fresh cbmc invocation per enclosing function) and because
synthesised harnesses are less well-shaped than hand-written
ones — large enclosing functions frequently time out.  It is
most useful when you want to confirm a specific prefilter hit
reproduces the bug-class pattern inside its actual enclosing
function.

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

### Corpus scan matrix (nightly)

For scale-out across LTS kernels, the
`corpus-scan-matrix` job runs `corpus-scan.sh` in a
parallel matrix across

- Linux 5.10 (CORPUS_MAX=120, fully shaken baseline),
- Linux 6.1 / 6.6 / 6.12 (CORPUS_MAX=60 each).

Each matrix row fetches its kernel tree from
`git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git`
(cached per version), configures it with the standard
`scan/configure.sh baseline.config crypto-aead.config`
fragments, runs the full discovery sweep, and uploads the
per-kernel JSON + logs + cocci-hit SARIF as a matrix-distinct
artifact.  Rows are `fail-fast: false` so one kernel regressing
doesn't mask the others.  The matrix is gated behind `schedule`
and `workflow_dispatch`; PRs continue to use the
`scan-on-kernel` single-kernel flow.

A local mirror of the matrix lives at
[`scan/corpus-scan-matrix.sh`](corpus-scan-matrix.sh): runs
serially over `$HOME/linux_{5_10,6_1,6_6,6_12}` and aggregates
per-kernel summaries into one report.  Use it to characterise
scan behaviour before rolling a new kernel into the matrix.

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
