\file
# Python Module Support Plan

This document tracks the multi-step plan for adding comprehensive Python
module support to the CBMC Python frontend. Work on this plan is expected
to span multiple development sessions.

## Motivation

The benchmark results show that the frontend's inability to fully handle
stdlib and third-party modules is a primary source of remaining issues:

- **MISS cases** (7/51 benchmarks): frontend doesn't detect bugs because
  the relevant code paths live in un-modelled modules
- **FP cases**: under-constrained stubs produce spurious counterexamples
- **TOERR/OOM cases**: over-approximated stubs produce intractable formulas

Both solver back-ends (default refine-strings and `--cvc5`) are affected
in the same way, confirming this is a frontend/stub problem rather than
a solver issue.

## Current state

- The frontend currently reports `Unknown function`/`Unknown variable`
  warnings and returns nondet values for anything it can't resolve.
- The benchmark harness relies on `stubs-full-python/` — hand-written
  stubs in the benchmark repository (not this repo).
- Regression coverage does not include any real stdlib code.

## Plan

### Step 1 — Frontend can parse stdlib code (in progress)

**Goal:** The frontend can ingest any stdlib `.py` file without crashing,
infinite-looping, or producing uncontrolled errors. Warnings about
unknown names are acceptable at this stage.

**Tasks:**
- Enumerate the Python language features used by representative stdlib
  modules (`urllib.parse`, `collections`, `json`, `string`, `functools`,
  `re`, `datetime`, `dataclasses`).
- Fix frontend gaps: `classmethod`/`staticmethod` decorators, complex
  inheritance, `*args`/`**kwargs`, module-level complex expressions,
  `__slots__`, walrus operator, match statements, etc.
- Create `integration/python-stdlib/` with a test that parses each
  target stdlib module and asserts no crash and bounded wall time.
- Wire the integration test into CI so stdlib parsing does not regress.

**Exit criterion:** The integration test parses all targeted stdlib
modules within a bounded time budget, with no crashes.

### Step 2 — Model library with fallback to CPython source

**Goal:** Users can verify programs that import stdlib modules, using
verification-optimized models by default and falling back to the real
CPython source on demand.

**Tasks:**
- Create `src/python/library/` containing simplified models of stdlib
  modules (e.g., `urllib/parse.py` returning nondet named tuples with
  the right structure, rather than the full 1258-line CPython
  implementation).
- Extend the frontend's import resolution to check the model library
  first, then fall back to the system CPython stdlib location
  (`sysconfig.get_paths()['stdlib']`).
- Add a command-line option (tentatively `--python-use-stdlib-source`)
  and/or honor `PYTHONPATH` to let users force the real CPython source.
- Extend the Step 1 integration test to run in both modes.

**Exit criterion:** The 1 real FP case (`websocket_url_validator`,
which uses `urllib.parse.urlparse`) is eliminated by the model library.

### Step 3 — Python stub infrastructure

**Goal:** Establish the conventions and helpers needed for writing
high-quality stubs.

**Tasks:**
- Document the stub authoring conventions (type annotations, nondet
  helpers, `__CPROVER_assume` usage, structural constraints).
- Provide helper primitives that stubs can use (e.g., a canonical
  `nondet_string(min_len, max_len)` helper).
- Ensure the frontend recognizes these helpers and lowers them
  efficiently.

**Exit criterion:** Writing a new stub is a predictable, low-friction
task governed by the documented conventions.

### Step 4 — Broader module support

**Goal:** Comprehensive coverage of the ecosystem surface used by
real-world Python programs.

**Candidate areas:**
- C-extension modules via reuse of CBMC's C library models (e.g.,
  `math.sin(x)` → `sin(x)`).
- Third-party stubs migrated into the repository from
  `python-verification-benchmarks/stubs-full-python/`, with
  tightened nondet constraints to eliminate the 4 debatable FPs
  (`aws_resource_tagger`, `aws_untagged_resources_analyzer`,
  `bedrock_model_discovery`, `kms_client_manager`).
- `re` module stubs (addresses 11 ESBMC-observed wrong-fails).
- `datetime`, `dataclasses`, `collections`, `json` — widely used
  across the benchmark corpus.
- Priority order: `urllib.parse` → boto3/botocore → `re` →
  `datetime`/`collections`.

**Exit criterion:** Module coverage is sufficient for the benchmark
suite to run without `PYTHONPATH` pointing to external stubs.

## Tracking

Sub-tasks should be tracked as TODO comments referencing this document
(`see doc/architectural/python-module-support-plan.md`) so that
progress across sessions remains visible.

## Front-end diagnostic verbosity

Ingesting real stdlib code exercises many front-end corner cases that
have a sound but imprecise fallback (e.g. returning a nondet value
when a call target is unknown). To keep default output actionable,
several of these paths are quiet by default and emit their diagnostic
at `log.debug()` instead of `log.warning()`. A single CLI flag,
`--python-strict-warnings`, promotes every one of them back to
warning level without touching any other verbosity knob.

Quiet-by-default diagnostics:

| Trigger                                              | Fallback              |
|------------------------------------------------------|-----------------------|
| Unresolved function call (`Unknown function …`)      | nondet return         |
| Unresolved method call (`Unknown method …`)          | nondet return         |
| Attribute access on opaque base (`attribute '…' …`)  | nondet return         |
| `Slice` expression used outside list/string subscript| nondet                |
| `Yield` / `YieldFrom` expression                     | nondet / inner value  |
| Subscript on an unsupported type                     | nil                   |
| `for … in <non-iterable>`                            | skip loop body        |
| `<item> in <non-list>`                               | `False` / `True`      |

All of these still exist as `log.debug()` output, so they can be
inspected with `--verbosity 9`, and all of them are re-emitted at
`log.warning()` when `--python-strict-warnings` is set. Anything that
is a genuine internal error (invariant violation, missing AST field,
an unknown variable name, an unsupported Python statement or
expression type not listed above) continues to log at warning /
error level regardless of the flag.

Users who are diagnosing a spurious verification result should run
with `--python-strict-warnings` to see exactly which parts of the
program were over-approximated.

## Baseline (2026-05-07, 120s timeout)

| Metric    | Default (refine-strings) | --cvc5 (structural) |
|-----------|--------------------------|---------------------|
| CLEAN     | 26                       | 25                  |
| TP        | 3                        | 1                   |
| MISS      | 7                        | 7                   |
| FP        | 5                        | 1                   |
| TOERR     | 5                        | 9                   |
| TIMEOUT   | 3                        | 3                   |
| OOM       | 2                        | 5                   |

Both back-ends are sound. MISS count (7) is identical across back-ends,
confirming it is a frontend/stub issue.
