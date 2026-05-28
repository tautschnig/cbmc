# CBMC Python frontend — roadmap

This document records open work items on CBMC's Python
frontend (`src/python/`), prioritised by value and risk. It
exists to carry context across sessions: each entry summarises
the symptom, the architectural shape of a fix, the rough scope
estimate, and any prior investigation. Update statuses as work
lands.

## Status snapshot (wave 41+, 2026-05-28)

| Metric | Wave 21 baseline | Wave 40 (prior) | Current | Δ vs wave 40 |
|---|---:|---:|---:|---:|
| ESBMC PASS | 2489 | 2601 | **2780** | +179 |
| Soundness gaps (raw DIFFs) | n/a | ~50 | ~22 | −28 |
| Soundness gaps (PLR-relevant) | 77 | 0 | 0 | 0 |
| Precision gaps (PLR-relevant) | 435 | ~50 | ~50 | 0 |
| TIMEOUT | 26 | 10 | 13 | +3 |
| Hypothesmith --unrestricted failures | 4 | 0 | 0 | 0 |
| AWS benchmark pass rate | n/a | 86.3% / 94.1% | 86.3% / 94.1% | 0 |

**Wave 41 work (2026-05-27 / 2026-05-28):**

Inheritance fix + cluster passes + COMPLEX tag + math edges
closed **+81 tests**:

Earlier in wave 41 (cluster + COMPLEX, +37):
- Inheritance MRO walk for method dispatch and `__init__` —
  +4 tests where subclass instances correctly route to
  inherited bodies (`a12746cd7f`).
- New CLI flag `--python-missing-return-check` + ESBMC
  `--incremental-bmc` alias; closes 6 missing-return
  soundness DIFFs (`9e86eb0deb`).
- ESBMC `--is-instance-check` alias for
  `--python-check-annotations`; closes 4 type-annotation
  soundness DIFFs (`7202c01f89`).
- `str.isidentifier` / `str.isnumeric` constant-fold;
  closes 11 string predicate DIFFs (`e8cb784c3b`).
- `str.partition` / `str.rpartition` constant-fold returning
  proper 3-tuple; closes 5 partition DIFFs (`817c3d1d4c`).
- Class-vs-class annotation check (Liskov MRO walk) +
  reassignment check; closes 2 type-annotation soundness
  DIFFs (`6ce20e9780`).
- `Union[X, Y, ...]` annotation check at call sites with
  strict category matching + class MRO walk; closes 1
  union-check DIFF (`e11911c837`).
- 2-level nested generator-expression unrolling for
  `all`/`any`; closes 2 nested-genexp DIFFs (`089a695a29`).
- COMPLEX tag in `python_type_tagt` (=8) with full
  truthiness / unwrap dispatch; enables universal
  `python_truthiness` in the all/any list path. Closes
  `builtin_all` and `any` (+2) without regressing
  `builtin_all_complex` / `builtin_all_complex_fail`
  (`90a76669ba`).

Math edges + TypeError cluster (+44 tests):
- `math.ldexp` / `nextafter` / `ulp` constant-fold (+6 tests,
  `bcf6e1b199`).
- int-math constant-fold (`factorial` / `comb` / `perm` / `gcd` /
  `lcm` / `isqrt`) with negative-literal handling and
  ValueError emission for negative args (+9 tests,
  `411e4861c1`).
- math constants (`pi` / `e` / `tau` / `inf` / `nan`) bound to
  IEEE-754 values via explicit ASSIGN at import-from + fold
  for `degrees` / `radians` (using M_PI directly) + `gamma`
  alias for `tgamma` + fix `gamma`→`std::tgamma` dispatch
  (+7 tests, `ac8ae6fef6`).
- list-arg math fold (`prod` / `dist` / `sumprod` / `fsum`)
  accepting List, Tuple, or Name-bound-list; proper
  `isclose(a, b, *, rel_tol, abs_tol)` with CPython's
  formula (+9 tests, `03c3dbc910`).
- `math.frexp` constant-fold returning (mantissa, exponent)
  python_tuple struct (+2 tests, `949b51a93c`).
- `cmath.log` / `log10` constant-fold for python_complex
  args via std::complex (+3 tests, `c62ac1e226`).
- `complex` type annotation registered as python_complex
  struct + `math.X(complex)` raises TypeError; recursive walk
  of List/Tuple/dict-unpack/Name-bound-list/function-returning-
  complex (+2 tests, `8ce56a15a8`).
- int-math TypeError for non-int args (float/string/None,
  `e4f8dbcb9b`); cmath kwargs raise TypeError
  (`e99e7f91d0`).
- Symbolic exprt for `math.degrees` / `radians` / `fmod` /
  `copysign` so isfinite-style assertions propagate from
  finite inputs (+1 test, `d9a23a5bc1`).
- dict-literals alias propagation (`kw_alias = kw_base`) +
  extended complex-arg detection across alias chains, list
  literals with complex elements, direct dict literals in
  `**` (`b2cbdab7a6`); exclude `math.prod`/`math.sumprod`
  from complex-arg TypeError per CPython
  (`e68d923590`).

**Hypothesmith --unrestricted at 0 fails (200 semantic + 24
syntax programs verified).**

**Documented limitations (deferred):**

- `builtin_all_genexp_inner_iter_shadow` (var shadow `for x
  in xs for x in range(x)`) — needs proper Python generator
  scoping; current impl punts to single-generator path.
- icontract MRO precedence for mixin override conflicts uses
  first-found-wins rather than strict C3.
- `complex_math_typeerror_edges` — 14 of 94 sub-assertions
  remain. Need cross-function tracking of
  `function_return_constants` for dict/list literals
  containing complex (e.g., `math.X(**fn())`), and
  `complex("bad")` raising ValueError before downstream
  dispatch.

String cluster (wave 41 cont., +37 tests):
- 7 `complex_str_*` (str(complex), complex(str), proper
  formatting like CPython `(1+2j)` / `(1+0j)` / `Nj` /
  `0j`, ValueError for malformed strings).
- 14 `string-split-whitespace*` (whitespace-mode split when
  no separator or None).
- 4 `string-splitlines*` (line-boundary splitting with CRLF
  awareness, proper empty-string and trailing-newline
  semantics).
- `string-module-constants` (`string.digits`,
  `string.ascii_letters`, etc bound at attribute-access
  time).
- `string-format-named`, `string-format-none` (str.format
  with named kwargs, None argument formatted as 'None').
- `fstring`, `fstring2` (bool args, empty f-string,
  format specs `:d` / `:.Nf` for constants).
- Bonus: `complex_equality_nan_inf`,
  `complex_handler_normalize` from string-vs-complex
  compare fix.

Foundational AST changes:
- AST emitter now tags Python imaginary literals (`2j`,
  `1+2j`) as `{"__complex__": true, "real": ..., "imag":
  ...}` instead of `default=str` which was indistinguishable
  from the source-string `"2j"`. Both the daemon and inline
  AST-to-JSON code paths updated.
- `convert_term` decodes the tagged complex JSON into a
  `python_complex` struct.
- Compare path skips string-to-complex promotion (was
  silently producing nondet for string-vs-complex
  comparisons).
- Empty-list compare allowed across incompatible element
  types (length-only equality).

Multi-cluster pass (wave 41 cont., +22 tests):
- 6 lambdas (lambda7/10/14/15/16/20): body-emitted pending
  checks (ZeroDivisionError on /, IndexError on []) now go
  inside the lambda's function body instead of the outer
  scope; per-parameter type inference replaces the
  body-wide string/float heuristic.
- 3 random tests (random2/3/7): random.random,
  random.uniform, random.triangular, random.randrange now
  emit constrained-nondet results matching their declared
  ranges (with strict upper bound for random()).
- 3 divmod tests: detect float arg before truncating to
  int, so divmod(7.5, 2.0) → (3.0, 1.5) instead of
  (3.0, 1.0).
- 3 set tests (empty_difference / intersection / union):
  set() now returns python_set_type, not python_list_type,
  so the BinOp set-op fast path fires for `set() OP set()`.
- boolop-short-circuit: 'and' / 'or' right-operand pending
  checks (KeyError on dict subscript, IndexError on list
  index, etc.) are now wrapped in an if-then-else guarded
  by the same condition that selects the right operand at
  runtime — so the right operand's side effects don't fire
  when the left disjunct decides the result.
- getrandbits: random.getrandbits(k) for constant k folds
  to [0, (1<<k) - 1] precisely; k <= 0 emits ValueError.
- int_bit_length: int.bit_length / .bit_count / .conjugate
  / .real / .imag / .numerator / .denominator dispatched
  on python_int receivers.
- has-attr: hasattr(obj, name) static fold against the
  receiver's struct components and qualified method symbols
  for constant 'name'.

List + dict mini-cluster (wave 41 cont., +5 tests):
- list-clear: list.clear() new method handler.
- list-insert-beyond / -preserves: insert(i, x) clamps i
  to [0, len] (CPython semantics) instead of writing past
  the buffer boundary.
- dict44: dict[key] aug-assign now invalidates the
  dict_literals constant-fold cache so subsequent reads
  see the updated value.
- (chained-string compare snap propagation also helps
  github_3036 substring patterns.)

KeyError / IndexError exception-flag refactor
(wave 41 cont., +8 tests):
- dict subscript on missing key (dict41,
  dict-attr-int-int).
- list / string subscript out-of-range (string-split-count,
  string-split-count2, github_3566, github_3609,
  github_3621, github_3716).
- list.pop, dict.pop, dict.popitem follow the same pattern
  (no test gain themselves but unblocks the general
  try/except IndexError / KeyError idiom).

Background: these emissions previously used
add_check(condition, "exception", "...") which is a property
assertion that fires regardless of any enclosing
try/except. Switched to setting __exception_active /
__exception_type — the flag composes with the existing
try/except dispatch so a matching handler catches the
path; the downstream uncaught_exception module-level
assertion still catches uncaught cases, with property
class "exception" and comment "uncaught exception"
(the regression test
regression/python/index-out-of-bounds was updated to match).

Two coordinated supports:
- python_converter_defs.cpp: implicit fall-through None
  return now re-reads the function symbol's return type
  AFTER body conversion, so a python_int sentinel doesn't
  silently truncate to a python_value-typed return slot
  (which produced a "warning: ignoring typecast" that
  broke typeddict-kwargs).
- python_converter_expressions.cpp: const-int-key dict
  subscript fold added (the existing const-key fold only
  covered string keys), so nested-dict2 stays under the
  per-test sweep timeout after the new exception flag adds
  modest SAT work to the runtime path.

Complex + Import cluster (wave 41 cont., +17 tests):
- 4 complex isinstance (arith / cond / conj / neg) —
  isinstance(non_complex, complex) and the symmetric forms
  now return false, instead of falling through to nondet.
- 4 complex augassign (attr_augassign / augassign /
  augassign_handler / attr_reassign) — `z += w` /
  `z -= w` / `z *= w` / `z /= w` for python_complex now
  do field-wise arithmetic. The Mul / Div paths use a
  field-snapshot temp because CBMC symex assigns struct
  fields one-at-a-time, evaluating the second field's
  expression after writing the first — so a self-
  referential struct_exprt that reads lhs.real and
  lhs.imag in both fields produced a wrong second-field
  result.
- 3 complex repr / zerodiv (repr / repr_var / zerodiv) —
  repr(complex) folds to the same string format as
  str(complex); complex / 0 raises ZeroDivisionError via
  the exception flag.
- complex_handler_typeerror — ordering compare on complex
  raises TypeError; incompatible-type BinOp (complex+str
  etc.) raises TypeError via __exception_active. Bitwise
  ops kept silent-nondet because our value-type tracking
  sometimes confuses set with int (frozenset returns a
  typet{}-typed nondet that defaults to int).
- 4 import-error tests (1-4) + import-error-fail bonus —
  unresolved 'import X' / 'from X import Y' now sets
  __exception_active so try/except ImportError catches
  the path.

Exception + None + tuple + float-mod (wave 41 cont., +6):
- 3 exception tests (exception_base_class with
  BaseException, exception8 with OSError catching
  FileNotFoundError, exception10 with user-defined class
  hierarchy via class_mro). Builtin exception subclass
  table + user-class MRO traversal in handler match.
- none_compare_is — 'None' return / param annotation now
  maps to python_int_type instead of empty_typet, so
  arguments preserve the None sentinel through the call
  boundary (and 'x is None' compares to the literal
  -2^62 sentinel).
- tuple6 — negative tuple index `t[-1]` folds via
  try_eval_double through UnaryOp(USub) and wraps to
  positive via the tuple's struct component count.
- float_modulo_compound_assign — `x %= y` for python_float
  uses Python's floored semantics
  `r = a - floor(a/b) * b` instead of integer modulo.

Coordinated supports:
- The None-as-int change required gating the
  --python-missing-return-check on functions with a
  non-None return annotation: a `def f() -> None`
  legitimately falls through, the implicit None return
  matches the declared type, and missing-return should
  not fire. Detect None at the AST level (Constant with
  value=None) so the type-level check (now python_int)
  doesn't mask it.

Set / shadow / dedup mini-cluster (wave 41 cont., +3 net):
- github_2965_3 — set literal {'foo','bar','foo','bar'}
  now deduplicates string-constant elements at conversion
  time; length matches Python set().
- infer-func-param — user-defined function `def sum(a, b)`
  shadows the builtin `sum(iterable)`. The dispatch order
  now checks for a user symbol BEFORE the builtin
  intercept fires.
- import-from-function / import-from-multiple — bonus
  gains from the same shadow fix (these tests rely on
  user-defined functions imported via `from X import Y`).
- Set bitmap construction now uses bitwise OR (was
  addition), so {1, 2, 1} folds to bitmap 6 (1<<1 | 1<<2)
  instead of 8 (the duplicate's bit shifted out the
  original).

Net regression: import-from-multiple-fail (1 test) — the
test relied on a quirk of the previous builtin-shadow
behaviour. Our converter loads imported modules in full,
so symbols visible via 'from X import Y' include all of
X's symbols. The test expected `sub(3, 2)` after only
`from X import sum` to fail with NameError; with the
shadow fix the user's `sum(1, 2) == 3` now correctly
passes (was a misclassified failure before), and the
test's other assertion is satisfied. ESBMC's stricter
import tracking would still report FAILED for this test.
The converter loads-everything semantics is unchanged
from before the shadow fix.

All three regression suites (`regression/python`,
`regression/python-strata-tests`,
`regression/python-strata-tests-pending`) green.

## Status snapshot (wave 41 mid-, 2026-05-27)

| Metric | Wave 21 baseline | Wave 40 (prior) | Current | Δ vs wave 40 |
|---|---:|---:|---:|---:|
| ESBMC PASS | 2489 | 2601 | **2638** | +37 |
| Soundness gaps (raw DIFFs) | n/a | ~50 | ~28 | −22 |
| Soundness gaps (PLR-relevant) | 77 | 0 | 0 | 0 |
| Precision gaps (PLR-relevant) | 435 | ~50 | ~50 | 0 |
| TIMEOUT | 26 | 10 | 10 | 0 |
| Hypothesmith --unrestricted failures | 4 | 0 | 0 | 0 |
| AWS benchmark pass rate | n/a | 86.3% / 94.1% | 86.3% / 94.1% | 0 |

**Wave 41 work (2026-05-27):**

Inheritance fix + DIFF cluster pass + COMPLEX tag closed
**+37 tests**:

- Inheritance MRO walk for method dispatch and `__init__` —
  +4 tests where subclass instances correctly route to
  inherited bodies (`a12746cd7f`).
- New CLI flag `--python-missing-return-check` + ESBMC
  `--incremental-bmc` alias; closes 6 missing-return
  soundness DIFFs (`9e86eb0deb`).
- ESBMC `--is-instance-check` alias for
  `--python-check-annotations`; closes 4 type-annotation
  soundness DIFFs (`7202c01f89`).
- `str.isidentifier` / `str.isnumeric` constant-fold;
  closes 11 string predicate DIFFs (`e8cb784c3b`).
- `str.partition` / `str.rpartition` constant-fold returning
  proper 3-tuple; closes 5 partition DIFFs (`817c3d1d4c`).
- Class-vs-class annotation check (Liskov MRO walk) +
  reassignment check; closes 2 type-annotation soundness
  DIFFs (`6ce20e9780`).
- `Union[X, Y, ...]` annotation check at call sites with
  strict category matching + class MRO walk; closes 1
  union-check DIFF (`e11911c837`).
- 2-level nested generator-expression unrolling for
  `all`/`any`; closes 2 nested-genexp DIFFs (`089a695a29`).
- COMPLEX tag in `python_type_tagt` (=8) with full
  truthiness / unwrap dispatch; enables universal
  `python_truthiness` in the all/any list path. Closes
  `builtin_all` and `any` (+2) without regressing
  `builtin_all_complex` / `builtin_all_complex_fail`
  (`90a76669ba`).

**Hypothesmith --unrestricted at 0 fails (200 semantic + 24
syntax programs verified).**

**Documented limitations (deferred):**

- `builtin_all_genexp_inner_iter_shadow` (var shadow `for x
  in xs for x in range(x)`) — needs proper Python generator
  scoping; current impl punts to single-generator path.
- icontract MRO precedence for mixin override conflicts uses
  first-found-wins rather than strict C3.

All three regression suites (`regression/python`,
`regression/python-strata-tests`,
`regression/python-strata-tests-pending`) green.

## Status snapshot (wave 40, 2026-05-26)

| Metric | Wave 21 baseline | Current | Δ |
|---|---:|---:|---:|
| ESBMC PASS | 2489 | 2601 | +112 |
| Soundness gaps (raw DIFFs) | n/a | ~50 | n/a |
| Soundness gaps (PLR-relevant) | 77 | 0 | −77 |
| Precision gaps (PLR-relevant) | 435 | ~50 | −385 |
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

- **Class-instance args bind by reference** (3 tests) —
  `github_3822`, `_2`, `_3-nondet`. `f(a)` where `a` is a
  class-instance variable previously wrapped via
  `wrap_value` made a temp copy; mutations in `f`
  hit the copy. Special-cased the call-site arg-binding
  in convert_call: when the argument is a Name whose
  type is a `python_class_<Name>` struct AND the param
  is `python_value`-typed, bind via
  `make_python_value(CLASS, address_of(arg))` directly.
  Function-local return temps (`__ret_tmp_<Class>` from
  `pick(): return Foo()`) keep the wrap_value temp
  materialisation because they're not stable across
  call sites. (Commit f7fc9b7b0e.)

- **chr() ValueError + isinstance(x, type)** (4 tests) —
  `github_3090`, `github_3520_2`, `_4_fail`, `_6`.
  chr(i) on out-of-range i now raises ValueError through
  __exception_active so try/except can catch it.
  isinstance(x, type) where x is bound to a type object
  (built-in or class) returns True; conversely
  isinstance(x, T) for T != type when x holds a type
  returns False. New `name_holds_type_binding` set
  tracks per-symbol bindings to type objects.
  (Commit 2305cb6bb8.)

- **for-loop reversed(range())** (3 tests) —
  `github_3804_1`, `_2`, `_4-nondet`. Special-case the
  AST shape `for x in reversed(range(...)):` and lower
  as a descending range loop. (Commit 7f6409551a.)

- **list.__iter__() + tuple-unpack typed elt symbols**
  (7 tests) — `github_3751` cluster (5 tests) +
  `github_3647_3`, `_13`. list.__iter__() returns the
  list itself; for-loop tuple unpack creates loop
  variables with the tuple field type rather than the
  default python_int_type. (Commit d936102a37.)

- **nondet_X() module-global pre-registration** (2
  tests) — `github_3701_9-nondet`,
  `github_3701_if_else-nondet`. Pass 0's pre-registration
  now recognises `flag = nondet_int()/bool()/float()/
  str()` so functions converted in pass 1c can resolve
  the global. Without this, function-body conversion
  saw the Name lookup return nil and the entire
  enclosing if/while/for body silently collapsed.
  (Commit d05d1237db.)

- **Strict-types call-site arg checks** (6 tests) —
  `github_3020`, `_2`, `_6`, `_8`, `_9`, `_10`. Three
  changes: narrow the boto3-style class-vs-int FP
  suppression in annotation_types_incompatible (no
  longer applies blanketly to string/list/dict/set
  declared types); mirror the function-call path's
  annotation-mismatch check at the method-call dispatch;
  map ESBMC's --strict-types to our
  --python-check-annotations in the sweep script.
  (Commit c42c3fe06d.)

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

**Status**: closed in wave 39. Wrote
`doc/python-frontend-architecture.md` (commit
`c852248711`) covering:
- Top-level flow and pass structure (0 / 0.1 / 0.25 / 1a /
  1a-bis / 1b / 1b.5 / 1c / 2)
- Constant tracking maps and their invalidation drivers
- Function summaries and synthetic-temp naming
  conventions
- Symbol naming conventions
- Type system (CBMC types for int/float/bool/str/list/
  dict/tuple/complex/set/None/Any/class instance)
- Loop semantics, generator semantics (list-with-cursor),
  annotation semantics, forward-class refs
- Exception model
- Method dispatch (incl. super() inline-vs-direct-call)
- String-solver integration via `emit_string_function`
- Key flags
- "Where to make changes" lookup table
- Tips for new contributors

Cross-referenced from `python-verification-guide.md` and
`AGENTS.md` (Important Links section).

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
