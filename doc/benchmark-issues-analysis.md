# Python Verification Benchmarks — Remaining Issues Analysis

Categorization of the 14 non-CLEAN/non-TP benchmarks (with
`--python-no-exception-checks`) by root cause type.

## 1. Python correctness issues (PLR precision)

Our analysis is imprecise or semantically not-quite-right.

### Path-sensitive dict-key tracking missing
- **Benchmark**: `bedrock_model_discovery` (FP)
- **Pattern**: `if K not in D: D[K] = []; D[K].op()`. After the
  if, K is in D in both branches. Our flow-insensitive analysis
  over-approximates possible KeyError.
- **Estimated fix**: ~150 lines path-sensitive analysis, or
  more limited pattern recognition.

### Runtime-built dict spread
- **Pattern**: `d = {}; d['k'] = v; f(**d)`. The `**d` spread
  only populates kwargs for compile-time-known dict literals
  via `dict_literals` tracking. Runtime-mutated dicts
  under-populate.
- **Estimated fix**: extend dict_literals to track
  incremental inserts across basic blocks (~80 lines).

### Inter-procedural dict-content tracking
- **Pattern**: `def f(): r = {}; r[K] = V; return r`.
  Caller's `result = f()` loses key tracking — our
  `function_returned_dict_keys` only captures direct
  dict-literal returns, not incrementally-built ones.
- **Estimated fix**: more general data-flow (~100 lines).

### `len()` on CLASS-tagged tagged-union
- **Issue**: We read .length at offset 0 assuming dict/list/string
  layout. Works because those share the layout, but user classes
  with CLASS-tagged __class_ptr have arbitrary layouts — could
  produce garbage for edge cases.
- **Estimated fix**: proper DICT/LIST/STRING tag instead of
  CLASS + offset trick (~60 lines, restructures tagged-union).

### Method dispatch on struct_tag vs struct
- **Issue**: Class instances from type annotations have
  struct_tag_typet; direct class struct instances have struct_typet.
  Two code paths now do similar work.
- **Estimated fix**: unified resolution (~40 lines).

## 2. Stub-specific issues (PySpec completeness)

No amount of frontend/solver improvement would help.

### `boto3.resource()` API not modeled
- **Benchmark**: `clear_duplicate_dynamodb_entries` (MISS)
- **Issue**: Stubs encode only `boto3.client()`. Bug is
  `len(Table)` — Table has no `__len__`.
- **Estimated fix**: stub work, ~200 lines for basic resource
  surface.

### Metric-specific dimension requirements
- **Benchmark**: `check_storage_costs` (MISS)
- **Issue**: Generic regex preconditions can't encode
  per-metric semantic rules.
- **Estimated fix**: stub-format extension, large scope.

### MediaConvert endpoint_url requirement
- **Benchmark**: `mediaconvert_manager` (MISS)
- **Issue**: Per-service ambient constraints aren't expressed.

### Stub return type `-> None` vs runtime dict
- **Benchmarks**: many
- **Issue**: Boto3 stubs declare `-> None` but the runtime
  returns a dict. Downstream `response['Items']` is nondet.
- **Relationship to annotation trust**: IMPORTANT — see section
  below. This is arguably a stub-completeness issue: the stubs'
  bodies don't return anything (no `return` statement), so
  Python semantics is consistent with the `-> None` annotation.
  It's NOT a case of us trusting annotations over body — both
  say None. The mismatch is stub-vs-real-API, not
  stub-body-vs-stub-annotation.

### Regex preconditions as validation proxies
- **Issue**: Stubs use `compile(pat).search(str) is not None`
  as "string matches pattern". Semantics is lost in the form.

## 3. Scalability issues (solver / CBMC internal limits)

### String-refinement solver on `.split()` of nondet
- **Benchmark**: `websocket_url_validator` (FP)
- **Issue**: 64-element nondet list × regex comparisons to
  literals → solver UNSAT cascades.
- **Potential mitigations**:
  - `--python-regex-nondet-true` flag replacing `compile(...).match/search(...)` with True.
  - Reducing PYTHON_MAX_LIST_LENGTH for nondet-split fallback.
  - SMT theory of strings instead of refinement.

### Pre-existing (now mitigated by stub-body-skip):
- Stub-body regex cascades
- Unbounded unwinding on pagination loops
- Memory pressure from 16-entry dict scans

## 4. Design / misc

### Broad `except Exception` absorbing detected errors
- **Benchmarks**: `bedrock_data_automation_example`,
  `test_bedrock_guardrails` (MISS)
- **Issue**: Our missing-method detection correctly raises
  AttributeError. User source has `except Exception` which
  correctly catches it. No analysis bug — this is **benchmark
  design**: the author classified these as "buggy" expecting
  semantic detection beyond exception reachability.

### `--python-no-exception-checks` trade-off
- The flag trades exception-propagation detection (4 TPs) for
  clean CLEAN verdicts on reachable-but-intentional raises.
  Domain-dependent.

### Classification criteria
- Benchmark `.expect.detected.todo-strata-*` suffixes encode
  tool-specific limitations. Translating our capabilities to
  the grader's matrix is non-trivial.

## Summary by category

| Category | Count | Examples |
|----------|-------|----------|
| 1. PLR precision | 1 FP + latent | bedrock_model_discovery |
| 2. Stub completeness | 4 MISS | check_storage_costs, mediaconvert_manager, clear_duplicate_dynamodb_entries |
| 3. Scalability | 1 FP | websocket_url_validator |
| 4. Design/misc | 6 MISS + 1 FP | AttributeError-swallowed cluster |

## Meta-observations

- ~29% remaining issues are genuine frontend limitations
  (cat 1+3).
- ~29% are stub-level (cat 2) — no frontend improvement
  helps.
- ~42% are design/benchmark-grading trade-offs (cat 4).

Further gains on this benchmark suite come mostly from
either stub refinement or benchmark-aware configuration
matching the grader's expectations of what counts as a bug.

## Key open question: annotation trust

Python doesn't enforce type annotations at runtime. Our
frontend currently uses annotations in several places:

- **Variable types**: `x: int = 5` sets x to int type.
- **Parameter types**: `def f(x: int)` constrains x's type.
- **Return types**: `def f() -> int` declares return.
- **Class field types**: class-level annotations shape the struct.

Potential correctness issue when the body contradicts the
annotation (legal Python but semantically divergent from
what we model). To investigate systematically.
