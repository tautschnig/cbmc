# CBMC Python frontend — roadmap

This document records open work items on CBMC's Python
frontend (`src/python/`), prioritised by value and risk. It
exists to carry context across sessions: each entry summarises
the symptom, the architectural shape of a fix, the rough scope
estimate, and any prior investigation. Update statuses as work
lands.

## Status snapshot (wave 38, 2026-05-26)

| Metric | Wave 21 baseline | Current | Δ |
|---|---:|---:|---:|
| ESBMC PASS | 2489 | 2576 | +87 |
| Soundness gaps (raw DIFFs) | n/a | 56 | n/a |
| Soundness gaps (PLR-relevant) | 77 | 0 | −77 |
| Precision gaps (PLR-relevant) | 435 | ~70 | −365 |
| TIMEOUT | 26 | 10 | −16 |
| Hypothesmith --unrestricted failures | 4 | 0 | −4 |

**Soundness-gap accounting**: 56 raw DIFFs where the test
expects FAILED but we report SUCCESSFUL. All fall into
out-of-scope categories: opt-in strict-types-flag tests,
missing-return / type-annotation enforcement (PLR doesn't
require enforcement), import-error detection,
math-edge-cases, github_3287 fail-shape-specific,
ESBMC-nondet primitives (typed-element work documented
under #4 follow-up). The PLR-relevant soundness count
hits **0** in wave 38 — every gap that tests real Python
semantics has been closed.

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

**Status**: partial — `string-concat4` closed in wave 34
via the AugAssign string-solver routing. The remaining 3
tests in the cluster fail for orthogonal reasons:
- `string-concat5`: `assert word[0] == "a"` — indexing into
  the cprover-string-concat result. The string solver
  currently exposes the result opaquely, so word[0] reads
  as nondet.
- `string-concat6`: `for char in s: if char == ",": result.append(word) else: word += char`
  — list.append of a mutated string + multi-iteration
  reasoning.
- `string-concat13`: `s = ""; alphabet = string.digits +
  string.ascii_uppercase; s = alphabet[0] + s`. Misses the
  literal value of `string.digits` from the imports
  pipeline.

**Fix shape (to close 5/6/13)**: extend the string-solver
glue so word[i] reads on a concat result are resolvable at
the cprover-string layer; populate `string.digits` /
`string.ascii_uppercase` constants in the library hooks.

### 4. ESBMC-nondet primitives (12-13 tests)

**Status**: substantially closed. Default bounds (15/8/8)
in wave 34; typed-element kwargs in wave 37.

**Closures (wave 34)**: nondet_str (default bound 15),
nondet_dict6, nondet_dict13_fail (closed via default
bound 8).

**Closures (wave 37)** — typed-element parsing in
convert_call:
- `nondet_list(N, sample)`: second positional arg's type
  becomes the list element type.
- `nondet_dict(N, key_type=K, value_type=V)`: kwargs name
  the key/value sample types.
- nondet_list11, nondet_list13, nondet_list14, nondet_dict10,
  nondet_dict10_fail, nondet_dict12, nondet_dict12_fail,
  nondet_dict13.

**Open follow-up**:
- `nondet_list4`: `assert x[0] is not None` — typed-int
  nondet element can take the None-sentinel value (-2^62);
  fix would exclude that value from typed nondets, which
  is an under-approximation.
- `nondet_list17/18`: append-then-index of strings; the
  string-solver / list-write-read interaction is a
  separate issue.
- `nondet_list5`: complex iteration over bounded nondet
  list; loop-unwinding sensitivity.
- `nondet_dict14`: `k in x` membership on string keys
  with nondet content; needs richer string-solver
  modelling.

### 5. Profile and address TIMEOUT tests (12 tests)

**Status**: partial. Sort fast-path closed 4 in wave 34
(sorted4, sorted4_fail, list-sort9, list-sort10). 8 remain
TIMEOUT:
- `dict65`, `github_3626`, `github_3667_2`, `github_3684`,
  `list31`, `nondet_list6`, `shedskin`,
  `string-nondet-in-success`.

**Why 4 closed**: sorted() and .sort() previously emitted
an O(n²) bubble sort that issued a string-solver
comparison per pair per pass. Added a constant-fold path
that sorts at conversion time when all elements are
constants (int or string).

**Fix shape (for the 8 remaining)**: each is a different
shape:
- `github_3684`: dict iteration over `.items()` of a typed
  literal dict — symex paths through the schema-walking
  code blow up.
- `list31`: long sequence of function calls returning
  varied list/dict/tuple shapes; type-promotion paths
  multiply.
- `dict65`, `nondet_list6`, `string-nondet-in-success`:
  symbolic sizes interacting with bounded loop unrolling.

These are not single-fold candidates; each needs a profile
to find the hot path. Track per test as a follow-up.

---

## Long-tail items (varied scope)

### 6. github real-world cluster (~20 open tests)

**Status**: ~14 closed across waves 35-36. Open count
~129 (down from 143). Sub-clusters fixed:

- **Forward-class references** (5 tests) — `github_2997`,
  `_4`, `_5`, `_6`, `_7`. Class A method returns 'B' where
  B is defined later in source. Sub-pass 1a-bis re-runs
  convert_class_def for affected classes; idempotent
  symbol/temp type refresh; safe_typecast pointer-to-struct
  dereference. (Commits c96c45d79c, 4f2a23ebe8.)

- **Class fields are attributes** (1 test) — `github_3305`.
  Any-arg attribute check used class_declared_methods,
  missing class-level annotated fields. Extended to also
  consult class_types[name].components(). (Commit
  abed4795f4.)

- **dict.items() runtime tuples** (5 tests) —
  `github_3647`, `_2`, `_6`, `_7`, `_8`. items() on
  non-literal dicts returned nondet; for-loops then bound
  k/v to nondet. Construct a python_list of
  python_tuple(keys[i], values[i]) of length obj.length.
  (Commit db10df94e7.)

- **super() value-returning methods** (3 tests) —
  `github_3838_2`, `_3`, `_4-nondet`. super().method() was
  unconditionally inlined as pending_checks; the inlined
  base body's `return X` short-circuited the caller's
  function. Replaced inlining with a direct CALL to
  `<base>::<method>` for non-__init__ super calls.
  (Commit bbb5b3cded.)

- **int(s, base) + arg-prop walker** (5 tests) —
  `github_3041_2_*`. Two-part fix:
  (a) extended `int()` to handle the optional base
      argument (0/2..36 with prefix detection); fixed
      a use-after-free on the temporary string;
  (b) sub-pass 1b.5's argument-propagation walker only
      inspected statement-level `Expr -> Call`, missing
      calls inside Assert/Assign/Return/etc. Replaced
      with a recursive scan_expr that traverses common
      AST node fields. The walker fix likely propagates
      constants into many more tests' parameters as a
      side effect. (Commit b88ea3f003.)

**Remaining open clusters** (sample):
- `github_3020_*` (7 tests): runtime arg-type-mismatch
  detection. Tests use `--strict-types` flag we don't
  support; closing requires implementing call-site
  arg-type-mismatch property under the existing
  `--python-check-annotations` flag.
- `github_3041_*` (5 tests): int(string, base) conversion
  edge cases.
- `github_3313_*` (3 tests): isinstance narrowing inside
  the function body for `str | datetime` union parameters
  + datetime stub field accesses.
- `github_3804_*` (4 tests): reversed(range(...))
  iteration.
- `github_3560_*` (4 tests): list index-out-of-range.
- `github_3287_*_fail` (3 tests): expecting FAIL but
  reporting SUCCESSFUL — soundness-shape tests we'd need
  to investigate per-test.

**Fix shape (for triage continuation)**: each remaining
cluster maps to a different architectural area
(call-site type checking, int parsing, union-narrowing,
reversed iteration, etc.). Tractable but no shared root
cause across them.

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
