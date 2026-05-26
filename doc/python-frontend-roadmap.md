# CBMC Python frontend — roadmap

This document records open work items on CBMC's Python
frontend (`src/python/`), prioritised by value and risk. It
exists to carry context across sessions: each entry summarises
the symptom, the architectural shape of a fix, the rough scope
estimate, and any prior investigation. Update statuses as work
lands.

## Status snapshot (wave 31, 2026-05-26)

| Metric | Wave 21 baseline | Current | Δ |
|---|---:|---:|---:|
| ESBMC PASS | 2489 | 2529 | +40 |
| Soundness gaps (PLR-relevant) | 77 | ~3 | −74 |
| Precision gaps (PLR-relevant) | 435 | ~107 | −328 |
| Hypothesmith --unrestricted failures | 4 | 0 | −4 |

All three regression suites (`regression/python`,
`regression/python-strata-tests`,
`regression/python-strata-tests-pending`) green.

---

## Architectural items (multi-session, high value)

### 1. Generators / `yield` (PLR §6.2.9)

**Status**: deferred since wave 19; the largest remaining
PLR-correctness gap.

**Symptom**: `github_3701_*` cluster (5-6 tests). Generator
expressions and `yield` statements are partially modelled —
yield in a function makes it a "generator" but the resumable
execution semantics are not properly represented.

**Why deeper**: generators underpin generator expressions,
`itertools.*`, async generators, and lazy iteration patterns.
Closing this unlocks more than the immediate cluster.

**Architectural shape**: state-machine encoding for resumable
functions. Each yield point becomes a state. The function
body is rewritten to dispatch on a hidden state variable.

**Scope estimate**: ~1 week of focused work.

**Direct closures**: 5-6 tests (github_3701_*); plus likely
unblocks downstream `itertools` test cases.

### 2. Annotations are documentation, not enforcement
(PLR §3.1, §3.2)

**Status**: open. ~5 precision gaps + 1 soundness gap.

**Two symptoms, same root cause.** Both come from the
converter treating annotations as runtime type assertions
rather than informational hints. Best fixed together —
splitting them risks two parallel mechanisms when the
underlying invariant is shared.

**Symptom A: scalar annotations** (`github_3775_{,2,3,4,5}`):
```python
def greet() -> str: return "Hi"
x: int = greet()
assert x == "Hi"      # Python: True. Us: False.
```
The converter casts the RHS to the annotation type, so
`x` becomes a (numeric coercion of) the string instead of
the actual `"Hi"`.

**Symptom B: collection-element annotations**
(`dict_subscript_typed_assign_fail`):
```python
d: dict[int, float] = {1: 1.0}
d[2] = "wrong-type"
isinstance(d[2], float)   # Python: False. Us: True.
```
The dict's declared value-type drives the read-back type
of `d[2]`; the actual stored string is lost.

**Architectural shape (shared)**: separate the *binding*
type from the *runtime* type. The annotation should drive:
- `__annotations__` queries
- type-completion / static-analysis surfaces
- defaults for nondet shapes when the runtime value isn't
  knowable at conversion time.

The annotation should NOT drive:
- value coercion at assignment
- the type of subsequent reads when the actual stored
  value's type is known
- `isinstance` / `type()` answers

**Concrete approach**: extend the existing constant-tracking
maps (`string_constants`, `float_constants`, `dict_literals`,
etc.) into a unified runtime-type map keyed by symbol id and,
for collections, by `(symbol, key)`. Subscript stores update
the entry; subscript reads / `isinstance` consult it before
falling back to the declared type. Annotation-driven casts
on plain assignment become no-ops when the RHS type is
already concrete.

**Scope estimate**: ~3-4 days (combined). Splitting into A
and B and doing only A would still leave B's compound
problem and likely require a partial second rework.

**Direct closures**: 5 precision gaps + 1 soundness gap.
Compounding effect on correctness for any code that mixes
typed annotations with runtime polymorphism — which is
common in real Python code.

---

## Bounded point-fix items (single-session, high confidence)

### 3. `string-concat` in loop (4 tests)

**Status**: open.

**Symptom**: `string-concat{4,5,6,13}`. Pattern: `s = ""; for x in xs: s += str(x); assert s == "..."`.
The loop-write invalidation now correctly clears
`string_constants[s]` on entering the loop, so the
concatenation can't fold.

**Fix shape**: post-loop reasoning — when a loop body's
augmented-assign sequence is bounded and the iterable is
known, fold to the unrolled concatenation. Or model the
string-solver loop primitive.

**Scope estimate**: 1-2 hours.

**Direct closures**: 4 tests.

### 4. ESBMC-nondet primitives (12-13 tests)

**Status**: open. Marked "ESBMC-only" but several are
PLR-aligned semantically and could be modelled.

**Symptom**: `nondet_dict`, `nondet_list*`, `nondet_string`,
`list_pop11_nondet_fail`. ESBMC has built-in primitives we
don't expose.

**Fix shape**: stubs in `src/python/library/` (or builtins)
that produce nondet structs of the requested shape. Use the
existing `nondet_*` symbol-table conventions where possible.

**Scope estimate**: ~3-4 hours per primitive type, but
multiple tests close per stub.

**Direct closures**: ~12 tests.

### 5. Profile and address TIMEOUT tests (12 tests)

**Status**: open. 12 tests time out at 60s with `--unwind 10`.

**Symptom**: tests that may verify correctly but slowly. Run
`scripts/profile_cbmc.py` on representative samples to see
if a hot path is the bottleneck.

**Fix shape**: depends on findings — could be loop-unwinding
explosion, string-solver thrashing, or O(n²) constant
tracking. The fix may be a single optimization that closes
several tests.

**Scope estimate**: 1-2 hours per representative test for
investigation; fix scope varies.

**Direct closures**: variable, potentially 5-12 tests.

---

## Long-tail items (varied scope)

### 6. github real-world cluster (~20 open tests)

**Status**: open. Each test maps to a real-world Python
program from a reported issue. Various shapes:
- argument-propagation through complex flows (like
  `github_2932`)
- string operations (`github_2965_set_unique`)
- arithmetic edge cases (`github_3041_*`)
- format edge cases not yet covered

**Fix shape**: depends on the test. Triage one cluster at a
time. The closures from session 2025-2026 (waves 22-31)
mostly came from this pool.

**Scope estimate**: varies; many are 1-2 hours, some need
deeper changes.

### 7. `lambda7` / `lambda18` body emission

**Status**: open since wave 21. Single-test issues.

**Symptom**: lambda body conversion leaks into the caller's
goto in some cases (concat operations notably). Worked
around for the struct-call dedup case in commit
`76172936a2`, but the underlying lambda-body emission bug
remains.

**Scope estimate**: ~1 day.

### 8. Architecture documentation

**Status**: not started. Frontend has stabilised through the
2026-05 session (nested-function naming, function-summary
cache, argument propagation, loop invalidation, dict
equality).

**Content to cover**:
- Symbol naming conventions: module-level vs nested vs
  parameter; scope qualification rules.
- Constant tracking maps: `string_constants`,
  `float_constants`, `dict_literals`, `list_literals`,
  `tuple_literals`, `complex_literals` — what each holds,
  when it's populated, when it's invalidated.
- Function summaries: `function_returned_dict_keys`,
  `function_returned_literal`, `function_return_count`,
  `function_aliases`, `lambda_returning_functions`.
- Closure captures: how nested functions see enclosing
  scope variables.
- Snapshot/restore for branch-local maps in if/match/try.
- Pre-scan passes (Pass 0.1, 0.25, 0, 1a, 1b, 1b.5, 1c) and
  what each populates.
- `qualify_name` semantics including `global` and
  `nonlocal`.

**Scope estimate**: ~1 day.

**Best done after**: one of items 1-2 lands so the docs
reflect a stable design.

---

## Tracking conventions

When picking up an item:
1. Update the **Status** line to "in progress, <commit ref>".
2. On completion, change to "done, <commit ref>" and move
   the entry to the Closed section below (create if needed).
3. If an item is split or refined, add new entries with
   refined descriptions and link the original.
4. Re-run the wave sweep
   (`scripts/cbmc_on_esbmc_python.py`) and update the
   snapshot table.

## Working principles (from session guidance)

- "Avoid point-fixes when we really need re-architecting or
  deeper fixes."
- "Guiding principle is correctness per the Python Language
  Reference."
- Always set `ulimit -v 8388608` before running tests;
  `CBMC_MEM_MB=2048` for the hypothesmith subprocess cap.
- All commits must pass `git-clang-format --binary clang-format-15 HEAD^`.
- Attribute commits with
  `Co-authored-by: Kiro <kiro-agent@users.noreply.github.com>`.
- Prefer curly-brace constructor syntax over parentheses
  where they are equivalent.
- Each fix lands with a focused regression test under
  `regression/cbmc/python-*`.
