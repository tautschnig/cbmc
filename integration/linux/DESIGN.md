# Linux kernel security analysis with CBMC

This document describes the design of a CBMC-based tool for pre-merge
static analysis of Linux kernel source code.  It supersedes the earlier,
uncommitted prototype in this directory (`verify_driver.sh`,
`analyze_driver.py`, `security_properties.h`, the several `*.md` files,
and the test scripts) which will be removed when the design is
implemented.

## 1. Purpose

Provide a pre-merge static analysis tool for the Linux kernel that
catches semantic bug classes which compilers, sanitizers, and purely
syntactic tools such as Coccinelle miss.  The primary use case is CI
gating of kernel patches before they reach a release branch.

## 2. Non-goals

- Reproducing individual historical CVEs as an end in itself.  CVE
  reproductions serve as regression tests for the tool, not as the
  product.
- Replacing Coccinelle, Smatch, or sparse.  They complement each other.
- Whole-kernel-tree verification on every run.  The tool operates on
  the files changed by a patch, plus scheduled sweeps over files that
  touch annotated primitives.
- Catching every bug class.  The tool targets classes where CBMC's
  symbolic execution plus function contracts offer value over cheaper
  tools.

## 3. Approach overview

Three complementary layers:

1. **Built-in checks (baseline).**  CBMC's existing `--bounds-check`,
   `--pointer-check`, `--signed-overflow-check`, etc., run per-function
   on goto binaries produced by `goto-cc`.  Cheap insurance for
   classical memory-safety bugs.

2. **Coccinelle prefilter.**  Per-property `*.cocci` rules identify
   functions that touch an annotated kernel primitive.  Coccinelle is
   fast and lossy; its output seeds the more expensive CBMC stage.

3. **Property-module verification (primary).**  For each annotated
   primitive, a module ships CBMC function contracts
   (`__CPROVER_requires` / `__CPROVER_ensures` / `__CPROVER_assigns`)
   expressing the primitive's correct-use protocol, any ghost state
   needed to track call-crossing invariants (e.g. page provenance),
   and provenance-source annotations that set the ghost state when
   objects enter the kernel from userspace or from the page cache.

   Kernel functions identified by the prefilter are verified with
   `goto-instrument --replace-call-with-contract` for the annotated
   primitive, so that the caller must discharge the primitive's
   requires-clause at the call site.  The primitive's contract is in
   turn validated against a reference implementation via
   `goto-instrument --enforce-contract`.  Loop contracts
   (`goto-instrument --apply-loop-contracts`) similarly abstract
   bounded loops in the property implementations.

## 4. Why CBMC on top of Coccinelle

Coccinelle matches syntactic patterns.  CBMC proves or refutes runtime
behaviour.  Concretely, for the "Copy Fail" bug class
(CVE-2026-31431):

- Coccinelle can flag `sg_chain(X, _, Y); aead_request_set_crypt(_,
  X, X, _);` as a syntactic pattern.  A refactored kernel that hides
  `sg_chain` behind a helper function defeats the pattern.  A CBMC
  contract on `aead_request_set_crypt` does not care how the caller
  arrived at the scatterlist: if the destination contains a page
  marked `PAGE_CACHE_RO`, the requires-clause fails.
- Coccinelle output is "this is a match."  CBMC output is "with input
  values A, B, C, assertion X at line N fails" — a reproducible
  counterexample with a concrete trace.
- Coccinelle cannot prove a negative.  CBMC can, within its unwinding
  bounds and stubbing assumptions.

The pipeline uses Coccinelle to prune the search space cheaply and
CBMC to produce ground truth expensively.  Each property module ships
a `*.cocci` alongside its contracts; both are reviewed together.

## 5. Function contracts as the specification language

An illustrative (and simplified) contract for `aead_request_set_crypt`:

```c
void aead_request_set_crypt(struct aead_request *req,
                            struct scatterlist *src,
                            struct scatterlist *dst,
                            unsigned int cryptlen, u8 *iv)
__CPROVER_requires(sgl_all_user_writable(dst))
__CPROVER_assigns(*req)
__CPROVER_ensures(req->src == src && req->dst == dst);
```

`sgl_all_user_writable` is a predicate provided by the property
module that follows scatterlist chain links and checks that every
reachable page carries the ghost provenance tag `PAGE_USER_WRITABLE`.

Verification of a caller then looks like:

```
goto-cc -o file.gb crypto/algif_aead.c ...
goto-instrument --replace-call-with-contract aead_request_set_crypt \
                file.gb file_abstracted.gb
cbmc --pointer-check --bounds-check file_abstracted.gb
```

The contract itself is proved consistent with a reference
implementation via `--enforce-contract`, independently and once.

Contracts are richer than link-level function replacement: they
participate in CBMC's modular proof methodology, they document the
correct-use protocol in a form reviewable apart from any
implementation, and they compose across libraries.

## 6. Scaling and deployment modes

CBMC on a real kernel function with full unwinding takes minutes to
hours.  Whole-tree pre-merge scanning is not feasible.  Realistic
modes:

- **Changed-files PR scan.**  Given a unified diff, identify kernel
  files touched, find plausible entry points in them (syscall
  handlers, `file_operations`, `proto_ops`, tasklet callbacks), run
  Coccinelle prefilter, dispatch CBMC on surviving combinations.
  Primary mode.
- **Scheduled sweep.**  Nightly or weekly, run across the subset of
  kernel files that touch any annotated primitive.  Catches bugs
  introduced by someone changing the primitive's assumptions in a
  different patch than the one adding a new caller.
- **Targeted manual runs.**  Developer tool invoked on a specific
  function.  Useful for debugging findings and for landing new
  property modules.

Per-function CBMC runs are bounded with `ulimit -v` and `timeout`.
Timeouts surface in the output report as "could not prove or refute
within budget," not as tool failures.  The overnight/precision
trade-off is intentional: the tool prioritises soundness-within-bounds
over turnaround time.

## 7. Module priority

Modules ordered by combined (a) historical CVE density, (b)
subsystems where Amazon contributes upstream, and (c) primitive
reusability:

1. **`page_provenance`, `scatterlist`, `aead`.**  Validates Copy Fail.
   AEAD is used by kTLS which is widely deployed.
2. **`pipe_buffer`.**  Validates Dirty Pipe (CVE-2022-0847).  Generic
   kernel primitive touched by many subsystems.
3. **`skb`, `iov_iter`.**  Used by every socket path; relevant to
   networking drivers such as ENA and EFA.
4. **`dma_mapping`, `iommu`.**  DMA correctness; broad relevance.
5. **`rcu`, `srcu`.**  Longer-term.  Modern bug class, more dependent
   on CBMC's evolving concurrency story.

A property module is not considered complete until:

- its contracts are committed in `integration/linux/properties/<name>/`;
- its Coccinelle rules are committed alongside;
- at least one CVE regression exercises it under
  `integration/linux/cve-*`;
- CI runs both the CVE regression and a smoke test of the contract
  against a reference implementation.

## 8. Kernel-version support and CBMC front-end work

The initial proof of concept (M0) uses Linux 5.10 because
`compile_linux.sh` already drives `goto-cc` on that tree and the
vulnerable code for CVE-2026-31431 is present there.

From M3 onwards we target a recent LTS kernel (initial choice: 6.12).
The CBMC C front-end is expected to hit issues on newer kernels:
`__attribute__` forms, `_Generic` dispatch, GCC builtins not yet
modeled, assembler constraint corners.  Each blocker is filed as a
discrete CBMC issue with a minimized reproducer; the fix lands in
`src/ansi-c/` or `src/util/` as a separate, small commit; the issue
is closed with a regression test in `regression/cbmc` or similar.

This is the one place the plan expects CBMC-core work, and it is
driven by concrete kernel-code blockers rather than speculative
feature additions.

## 9. Pipeline architecture

```
Patch / PR ──► identify changed .c files
             │
             ▼
       Coccinelle prefilter  (per property module × file)
             │
             ├──► no match    → skip
             │
             ▼
      select applicable property modules
             │
             ▼
      goto-cc kernel file + dependencies  → per-file goto binary
             │
             ▼
      harness generator  (per entry point × module)
             │
             ▼
      goto-instrument --replace-call-with-contract
                      --apply-loop-contracts
             │
             ▼
      cbmc  per function
             │
             ▼
      report aggregator  →  structured findings (JSON + HTML)
```

## 10. Repository layout

```
integration/linux/
├── compile_linux.sh              pre-existing, kept as-is
├── DESIGN.md                     this document
├── README.md                     short pointer to DESIGN.md and per-CVE dirs
├── properties/                   property module library
│   ├── page_provenance/
│   │   ├── page_provenance.h
│   │   ├── page_provenance.c     (reference implementation, enforced)
│   │   └── page_provenance.cocci (prefilter)
│   ├── aead/
│   │   ├── aead_contracts.h
│   │   ├── aead.cocci
│   │   └── README.md
│   └── ... (pipe_buffer, skb, dma_mapping, ...)
├── cve-2026-31431/               first CVE regression; see §11
├── cve-2022-0847/                Dirty Pipe, once pipe_buffer module lands
└── scan/                         pipeline driver
    ├── scan.py                   PR-scan entry point
    └── ...
```

## 11. CVE regression: structure

Per-CVE directory:

```
integration/linux/cve-XXXX-YYYYY/
├── README.md         one page: bug, fix commit, what this regression proves
├── model.h           shared abstract model (scatterlist, page, etc.)
├── harness_vuln.c    abstract harness reproducing the vulnerable shape
├── harness_fix.c     abstract harness reproducing the fixed shape
├── real_kernel.c     (optional, M3+) harness against the real kernel file
└── run.sh            runnable: exercises cbmc, checks exit codes + messages
```

`run.sh` returns 0 iff:

- the vulnerable harness produces `VERIFICATION FAILED` on exactly the
  expected assertion;
- the fixed harness produces `VERIFICATION SUCCESSFUL`;
- when a real-kernel harness is present, the same distinction holds on
  the real source file.

The `run.sh` scripts are wired into the repository's test setup once
the CMake integration for `integration/` lands (see M6).

## 12. Milestones

| ID | Scope | Estimate |
|---|---|---|
| M0 | Abstract model of CVE-2026-31431 landed as `cve-2026-31431/`.  Validates the property-module idea on the smallest possible model. | 0.5d (done in experiment; repo land-in part of this doc's commit) |
| M1 | Remove uncommitted prototype files (`verify_driver.sh`, `analyze_driver.py`, `security_properties.h`, various marketing `*.md`). | 0.5d |
| M2 | Property-module infrastructure: contract conventions, ghost-state conventions, `goto-instrument --replace-call-with-contract` plumbing, reusable harness shell. | 1-2w |
| M3 | First two property modules (`page_provenance`, `aead`) against real `crypto/algif_aead.c` from 5.10.  Stretch: same against 6.12. | 1-2w |
| M4 | PR-scan driver: diff → Coccinelle → harness generation → CBMC → structured report. | 1w |
| M5 | Second bug class module: `pipe_buffer` + CVE-2022-0847 regression.  Validates extensibility of the framework. | 1-2w |
| M6 | CI integration: PR gate runs M4 on changed files, nightly sweep over annotated-primitive subset. | 1w |
| M7 | Kernel-version bump to a current LTS (6.12 or newer).  Address CBMC C front-end blockers as distinct sub-commits. | 2-4w |
| M8 | Additional property modules in priority order (§7): `skb`/`iov_iter`, `dma_mapping`/`iommu`, eventually `rcu`. | ongoing |

M0–M1 are landable now.  M2–M6 constitute the investment that turns a
CVE-specific demo into a deployable tool.

## 13. Explicit non-additions

To avoid scope creep:

- No runtime / dynamic analysis.  Syzkaller covers that space.
- No attempt to upstream property modules into
  `scripts/coccinelle/` in the Linux tree; they remain part of CBMC.
- No marketing coverage percentages in any documentation.  The tool
  covers exactly the set of CVE regressions that exist in
  `integration/linux/cve-*`.
- No use of `--race-check` in single-threaded harnesses.
- No custom CBMC checks for kernel-specific concepts (e.g.
  `--page-provenance-check`).  Provenance is expressed via ghost
  state and user-level `__CPROVER_assert`; baking it into CBMC would
  be a layering violation.

## 14. Open questions

- Best `goto-instrument` invocation for large goto binaries.  The
  existing `compile_linux.sh` route produces goto binaries embedded in
  ELF object files; the scan driver needs a convenient way to extract
  per-function goto programs.
- Timeout budgets per function class.  A socket receive path probably
  needs more unwinding budget than a utility helper; the scan driver
  should support per-pattern budgets.
- Report format.  Candidate: SARIF for integration with existing
  static-analysis dashboards, plus a human-readable HTML rollup.
- Interaction with CBMC's symex caching.  Longer runs may benefit
  from `--symex-cache-dereferences`; this needs measurement.

## 15. References

- CVE-2026-31431 ("Copy Fail") — regression in
  `cve-2026-31431/`.
- CBMC function contracts: see `doc/cprover-manual/contracts-*`.
- Coccinelle: `http://coccinelle.lip6.fr/`.
- Linux kernel Coccinelle scripts: `scripts/coccinelle/` in the
  kernel tree.
