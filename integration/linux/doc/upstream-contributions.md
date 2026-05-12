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

## Investigated but not yet upstream-ready

### LIM-008 — `goto-instrument --generate-function-body` silent no-op

- **What.**  Some valid-looking regexes cause `goto-instrument
  --generate-function-body` to silently exit without producing an
  output file.  A wildcard `.*` triggers a regex-parse abort
  ("Mismatched '(' and ')'") — likely an ambiguity in the regex
  parser's capturing-group handling.
- **Why it's not yet ready.**  Root cause is in goto-instrument's
  regex matcher and/or the `--generate-function-body` dispatch
  loop.  Needs a careful reproducer + minimal fix.  Deferred: the
  `integration/linux` pipeline works around it by hand-writing C
  stubs rather than relying on regex-driven body generation, so
  the workflow is unblocked.
- **Next step.**  File a focused GitHub issue with the reproducer
  regex from the limitation entry.

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
