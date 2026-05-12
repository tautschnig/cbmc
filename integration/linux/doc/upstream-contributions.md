# Upstream CBMC contributions from the integration/linux effort

The integration/linux scan pipeline has uncovered several CBMC-level
issues (catalogued as `LIM-NNN` in `CBMC_LIMITATIONS.md`) that are
independently useful to fix upstream.  This document tracks the
status of each such fix.

## Ready to upstream

### 1. `goto-cc`: warn on called-but-not-linked-body symbols

Commits: `d35de05d3e` (fix) + `886665b001` (regression test).

- **What.**  At link time, `goto-cc` iterates over the called
  symbols in every body in the linked goto model; for each callee
  that has no body of its own (and is not a well-known CBMC
  library stub such as `__CPROVER_*` or `malloc`), it emits a
  `log.warning()` pointing at `--export-file-local-symbols` and
  the `__CPROVER_file_local_<file>_<sym>` mangled-name convention.
- **Why.**  Without the warning, `cbmc` silently replaces the call
  with a nondet-return stub.  When the missing body is the subject
  of analysis, the verification is **silently vacuous**.  LIM-009
  took about a week of diagnostic work to unmask under this
  pathology; the warning would have surfaced it at the first link.
- **Regression test.**  `regression/goto-cc-file-local/warn-
  missing-body/` — two TUs, one defines `static int foo`, the
  other declares `extern int foo` and calls it.  Final link with
  `-Wall` is expected to emit the warning; exit 0 is required
  (warning is advisory).
- **Testing.**  All 16 tests in `regression/goto-cc-file-local`
  pass.  All CORE `goto-cc*` and `contracts` tests pass.
- **Status.**  Ready for upstream PR.  No regression, orthogonal
  benefit to every downstream goto-cc user.

### 2. `cbmc --sarif-result`: SARIF 2.1.0 output for verification results

Already an upstream PR branch: `origin/sarif-ui` (`64e301b3b5`).

- **What.**  `cbmc` gains a `--sarif-result <file>` flag that
  emits SARIF 2.1.0 log files alongside its verification output.
- **Why.**  The de-facto standard for cross-tool security /
  analysis result reporting; enables GitHub Code Scanning
  integration (as demonstrated in Phase 3.2 of the Linux
  integration).
- **Status.**  Not authored by this effort; cherry-picked into
  the develop branch so `integration/linux/scan/scan.py` can use
  it.  Upstream merge is tracked by the `sarif-ui` branch PR.

### 3. Object factory: amortised allocation + tree-size cap (LIM-008)

Commit: `c86617c94c`.

- **What.**  Two independent fixes to `c_object_factory_parameterst`
  / `symbol_factoryt` / `symbol_table_baset`:
  - `symbol_table_baset::next_unused_suffix(prefix)` gains a
    per-prefix hint cache (moved up from the derived
    `symbol_table_buildert`), making repeated allocation under
    the same prefix amortised O(1) instead of O(N).
  - New `max_dynamic_object_instances` (default 1000, CLI flag
    `--max-dynamic-object-instances`) hard-caps the total number
    of dynamic allocations the object factory emits for one
    nondet-init root.  Complements the existing
    `max_nondet_tree_depth` cap which only fires on recursive
    chains.
- **Why.**  LIM-008 reproducer (`goto-instrument --generate-
  function-body af_alg_alloc_areq` on Linux 5.10's
  `crypto/algif_aead.c` goto binary) hung indefinitely before
  this commit; finishes in 2 seconds after.
- **Regression test.**
  `regression/goto-instrument/generate-function-body-deep-struct-cap/`.
- **Testing.**  All 15 CORE labels under goto-cc / goto-instrument
  / goto-harness / contracts / symbol-table pass; seven
  `integration/linux/` regressions remain green.
- **Status.**  Ready for upstream PR.  Both fixes are orthogonal
  wins for any downstream user of the object factory on
  non-trivial C programs.

## Investigated but not yet upstream-ready

### LIM-014 — goto-cc constant-folding pathologies on Linux 6.x headers

- **What.**  `__is_constexpr(x)` + `__builtin_choose_expr` and
  `?:`-with-missing-middle-operand inside `__aligned(...)` both
  mis-fold in CBMC's ansi-c front-end when the argument contains
  a runtime expression.  Manifests on Linux 6.x kernel headers
  (`<linux/bits.h>` `GENMASK_INPUT_CHECK` and `<linux/cache.h>`
  `__cacheline_group_begin_aligned`).
- **Why it's not yet ready.**  Front-end constant-folding logic
  is subtle and involves multiple classes in `src/ansi-c/`.
  Needs careful isolation of each idiom, regression tests under
  `regression/ansi-c/`, and checking against other kernel
  versions.
- **Workaround shipped in this repo.**
  `integration/linux/scan/fragments/scan-compat.h` overrides the
  offending macros to sound-but-loose values via `-include` after
  the kernel's own headers.  Kernel semantics unchanged; only
  some compile-time checks are disabled.

### LIM-016 — `goto-instrument --replace-call-with-contract` invariant violation

- **What.**  `get_contract` in
  `src/goto-instrument/contracts/contracts.cpp:593` compares the
  contract-declaration's `code_typet` with the function-
  declaration's `code_typet` via `irept::operator==`, which
  recurses into every sub-irep including the attached
  `spec_requires` / `spec_assigns` clauses.  The two types are
  structurally different (contract has extra sub-ireps), and
  the DATA_INVARIANT fires even when the signatures match.
- **Why it's not yet ready.**  The fix — add
  `code_typet::structurally_equal(other)` that strips contract
  sub-ireps before comparing, and switch the DATA_INVARIANT to
  it — is mechanically straightforward but needs a regression
  test that installs a contract on a function whose declaration
  doesn't have attached clauses and checks it succeeds.
- **Blocks:** the full LIM-013 resolution path (per-file harness
  generation from prefilter hits).  Without this fix,
  goto-harness-synthesised harnesses that call into kernel TUs
  whose static-inline contract targets have their own
  declarations trigger the invariant.  Documented in LIM-016 in
  CBMC_LIMITATIONS.md.

### (LIM-008 previously lived here; now RESOLVED + upstreamable,
see "Ready to upstream" above.)

## Not pursued (inherent to CBMC's design)

### LIM-012 — scan verdict non-monotonically dependent on `slice_preserve` [resolved in-repo]

The issue is that `--aggressive-slice` cannot see
`__CPROVER_requires` clauses as CFG edges, so stub bodies can be
dropped even though they write into state the contract predicate
reads.  Resolved in the integration/linux pipeline by switching
to a direct-call harness (LIM-012 path 2) which constructs a
kernel-layout input explicitly.

A full upstream fix would need aggressive-slice to treat
`__CPROVER_requires` argument-expressions as CFG-level
dependencies.  Substantial change with cross-cutting soundness
implications; left as a long-term CBMC front-end direction rather
than an upstreamable patch today.

## Principles used in shaping the upstream contributions

- **No Linux-specifics in the CBMC source change.**  The
  goto-cc warning code path is generic; the Linux integration
  is the motivator and is cited in the commit message, but the
  code does not special-case kernel builds.
- **Regression test lives in the same directory as the feature.**
  `regression/goto-cc-file-local/warn-missing-body/` groups with
  the other static-symbol mangling tests.
- **Warning level, not error.**  The new diagnostic is advisory;
  exit 0 is required.  Users who want to fail builds on this can
  combine with `-Werror`-style flags via the existing goto-cc
  message-handler infrastructure.
