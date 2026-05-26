# CBMC Python frontend — roadmap

This document records open work items on CBMC's Python
frontend (`src/python/`), prioritised by value and risk. It
exists to carry context across sessions: each entry summarises
the symptom, the architectural shape of a fix, the rough scope
estimate, and any prior investigation. Update statuses as work
lands.

## Status snapshot (wave 33, 2026-05-26)

| Metric | Wave 21 baseline | Current | Δ |
|---|---:|---:|---:|
| ESBMC PASS | 2489 | 2541 | +52 |
| Soundness gaps (PLR-relevant) | 77 | ~2 | −75 |
| Precision gaps (PLR-relevant) | 435 | ~96 | −339 |
| Hypothesmith --unrestricted failures | 4 | 0 | −4 |

All three regression suites (`regression/python`,
`regression/python-strata-tests`,
`regression/python-strata-tests-pending`) green.

---

## Architectural items (multi-session, high value)

### 1. Generators / `yield` (PLR §6.2.9)

**Status**: partial — 6 of 11 failing tests in the cluster
closed by the list-with-cursor model (commit
`cbmc-on-esbmc-python` HEAD as of 2026-05-26). 5 remain open
for unrelated reasons.

**Closed by the list-with-cursor commit**:
- `github_3701` (return-before-yield → next raises
  StopIteration)
- `github_3701_6` (sequential nexts return successive yields)
- `github_3701_7` (next + StopIteration handler)
- `github_3701_8` (conditional return before yield)
- `github_3701_10` (yield in while loop)
- `github_3701_12` (yield in while with if/else inside)

**Still open** — pre-existing issues, surfaced by the
generator tests but not generator-specific:
- `github_3701_9-nondet`, `github_3701_if_else-nondet` —
  module-global `flag` referenced inside the generator's
  `if flag:` condition causes the entire if/else body to
  drop. Not a generator bug; reproduces with any function
  that reads a module global. (`if True` works, parameter
  works, local nondet works.) **Fix shape**: free-variable
  resolution for if-conditions inside non-closure functions.
- `github_3701_2`, `github_3701_4`, `github_3701_5-nondet`,
  `github_3701_11` — `for x in g` over a generator + complex
  shape (assertions inside generator body using `rand[0]`,
  `len(l1)` etc.). Index-out-of-bounds on indirect list
  reads. **Fix shape**: list-shape propagation across
  function boundaries (we already do this for return-list
  literals; need to extend to bound-symbol shapes).
- `github_3701_14` — TOERR. Recursive function with
  `extend([1] + r)`; doesn't even use `yield`. Unrelated
  to generators.

**Architectural model implemented** (list-with-cursor):
- The generator function's body is rewritten so that each
  `yield X` becomes `__gen_result.append(X)`. The function
  returns the eager-collected list. (Pre-existing model,
  retained.)
- Each `g = gen()` call site allocates a hidden int cursor
  `__cursor_<flat_name>` initialised to 0.
- `next(g)` emits a pending check `if(cursor >= length)
  raise StopIteration; else cursor++` and returns
  `data[max(cursor-1, 0)]`.
- StopIteration is raised through the existing
  `__exception_active` / `__exception_type` infrastructure,
  so try/except handles it without further changes.

**Why list-with-cursor instead of state-machine**: a true
state-machine encoding would resume the generator's body at
each yield with restored locals. The list-with-cursor model
trades execution faithfulness for a much simpler encoding
that suffices for the verification properties the cluster
checks. It is sound for the eager model: if the body has no
side effects beyond yields, the list is the same as what
true Python would produce on full enumeration. Side effects
in the body (e.g. external `print()` calls) appear earlier
than true Python would emit them, but this is acceptable
for our verification purposes.

**Scope estimate (remaining)**: ~2-3 days for the
free-variable + list-shape work, which would close the rest
of the cluster.

**Architectural shape** (for follow-up): track free
variables read by a function's body during pre-scan; ensure
they resolve to the module-global symbol's value (or
nondet) at call time. Separately, for `for x in g` over a
known-generator-instance, the loop should iterate `g.data`
up to `g.length` (instead of using the symbol's declared
type bound, which is currently
`PYTHON_MAX_LIST_LENGTH`).

### 2. Annotations are documentation, not enforcement
(PLR §3.1, §3.2)

**Status**: closed in wave 33. Both variants implemented in
a single commit. 6 tests close (5 scalar + 1 collection
soundness). One scalar test (`github_3775_5`) remains open
for an unrelated subclass-method-self-mutation issue.

**What landed** (architectural shape, for reference):

Two related sites in the converter, sharing the principle
that annotations are informational and don't coerce values
at runtime.

*Scalar variant — convert_ann_assign:*

When an `AnnAssign` `x: T = expr` has a concrete RHS whose
type doesn't equal the annotation, the symbol's type is
widened to the RHS's type instead of safe_typecasting the
value. The pre-existing rule covered `ID_struct` RHS only;
the fix extends it to `ID_struct_tag` so that
refined-string and class-tag RHS types also widen. Gated
on `!python_check_annotations` so the opt-in
annotation-mismatch property still fires when the user
asks for it.

*Collection variant — convert_assign Subscript path +
convert_subscript expressions:*

A new per-key override map
`dict_runtime_value_overrides[dict_id][key_repr] -> exprt`
records the original RHS at typed-dict subscript-assign
sites where the stored value's type doesn't match the
declared element type and the key is a constant. The dict
subscript-read path consults the map before the storage
array, so `isinstance(d[k], V)` reflects the actual stored
value's type.

Cleared on:
- non-constant subscript-assigns to the same dict
  (conservative: any entry could be affected),
- type-matching constant assigns (the runtime value now
  agrees with the declared type so no override is needed).

**Closures** (wave 32 → wave 33):
- `dict_subscript_typed_assign_fail` (the lone PLR-relevant
  soundness gap remaining after wave 32)
- `github_3772`, `github_3775`, `github_3775_2`,
  `github_3775_3`, `github_3775_4`

**Open follow-up**: `github_3775_5` involves a subclass
method that mutates `self` while returning a string —
unrelated to annotation semantics. Tracked under item #6
(github real-world cluster).

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
