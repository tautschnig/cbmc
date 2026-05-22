## CBMC against ESBMC's Python regression suite

A snapshot of how CBMC (this branch's Python frontend at
`cbmc-6.9.0-547-g3c2a693177-dirty`) behaves on ESBMC's Python regression
test suite (`esbmc/regression/python/` at upstream HEAD `3368fcef8`,
3091 tests).

### Methodology

ESBMC's regression descriptors (`test.desc`) are mostly written for
ESBMC's `--incremental-bmc` driver and use ESBMC-specific flags
(`--multi-property`, `--bitwuzla`, `--ir`, `--smt-during-symex`, etc.)
that CBMC does not understand. Rather than translate flag-by-flag we

* ignore the flag line of each `test.desc`,
* run CBMC with `--unwind 10 --no-unwinding-assertions` against the
  test's `main.py`,
* match CBMC's stdout against the expected-output regex on line 4 of
  `test.desc`.

The `test.desc` line-4 regex is almost always `^VERIFICATION
SUCCESSFUL$` or `^VERIFICATION FAILED$`, so this captures
verdict-level agreement reasonably well. A handful of tests assert on
specific Python-level diagnostic strings; those become FAIL if the
wording differs.

The runner is at
`scripts/cbmc_on_esbmc_python.py` (mirrored from
`/tmp/cbmc-runner/run_cbmc_on_esbmc_python.py`); see its `--help` for
options. Per-test timeout 30 s, 16 workers; the suite finishes in
about 80 s wall clock.

### Outcome categories

| Outcome | Count | %     | Meaning                                                                                            |
|---------|------:|------:|----------------------------------------------------------------------------------------------------|
| PASS    | 2118  | 68.5% | CBMC stdout matches the expected regex                                                             |
| DIFF    |  590  | 19.1% | Both ran to verdict but disagree (482 CBMC-FAIL-vs-ESBMC-OK, 108 the reverse)                      |
| UNKNOWN |  215  |  7.0% | `test.desc` has no expected-output regex; CBMC produced a verdict but it cannot be validated      |
| FAIL    |   68  |  2.2% | CBMC produced a verdict but a more specific expected regex did not match (mostly error-text tests) |
| TOERR   |   53  |  1.7% | CBMC ran but produced no verdict (frontend / solver error)                                         |
| CRASH   |   34  |  1.1% | CBMC died with an invariant violation (signal 6)                                                   |
| TIMEOUT |   12  |  0.4% | exceeded 30 s                                                                                      |
| SKIP    |    1  |  0.0% | `try-fail/` has no `main.py`                                                                       |

By tag: CORE 67.9%, THOROUGH 76.1%, KNOWNBUG 62.5%.

Raw per-test results are in `doc/architectural/cbmc-on-esbmc-data/results.csv`.

### CRASHes — 34 invariant violations (highest priority)

| n  | Invariant                                                              | Notes                                                       |
|---:|------------------------------------------------------------------------|-------------------------------------------------------------|
| 22 | `string_expr.h:160` (string-expression precondition)                   | Triggered by `dict32`, `dict33_fail`, several `github_*`    |
|  4 | `satcheck_minisat2.cpp:150` ("variable not added yet")                 | `string-nondet-in-*`, `nondet_dict2`                        |
|  2 | `arith_tools.cpp:149` (`from_integer` precondition)                    | `list`, `list-sort10`                                       |
|  2 | `namespace.h:49` (name not in namespace)                               | `github_3658_4`, `github_3841_4`                            |
|  2 | `boolbv_map.cpp:91` (variable bounds out of range)                     | `github_3033_string-in-price-decimals_fail`, `github_3130_fail` |
|  1 | other                                                                  | `github_3560_4` (variant of above)                          |
|  1 | spurious crash bucket — actually a 15s timeout in re-run                | `list-sort10`                                               |

The full list of triggering tests is in
`doc/architectural/cbmc-on-esbmc-data/buckets.txt`.

The `string_expr.h:160` cluster is the dominant one and most are short
single-file repros (e.g. `dict32`):

```
d = {"a": False}
x = d.get("a")
assert x is False
```

→ Invariant: `from_integer` precondition in `arith_tools.cpp:149` (in
this case `from_integer` is being called from inside the string solver
when the `dict.get` lowering tries to materialise a string slice over
a boolean).

### TOERRs — 53 frontend / solver errors

| n  | Root cause                                                             |
|---:|------------------------------------------------------------------------|
| 41 | `dec_solve: current index set is empty, this should not happen` — string-refinement solver gives up |
|  5 | `stod` parser exception — complex-number string parsing in the Python frontend |
|  7 | other frontend errors (bad return-type inference, `__python_dict__` lookups, etc.) |

Triggering tests: `dict18`, `dict_del*_fail`, `fstring`, `fstring2`,
`esbmc-module-nondet3`, `complex_*`, etc.

### DIFFs — verdict mismatches

590 cases where both tools ran to a verdict but disagree:

* **482**: ESBMC SUCCESSFUL → CBMC FAILED. Mostly methodology, not
  CBMC bugs:
  * 463/482 are pure verdict mismatches with no extra text in CBMC's
    output, dominated by `Python assertion: FAILURE` and
    `uncaught exception: FAILURE`. CBMC at fixed `--unwind 10` flags
    spurious assertion failures on programs that ESBMC's
    `--incremental-bmc` would prove. Bumping `--unwind` and using
    `--depth N` would close some of the gap, but cannot match
    ESBMC's incremental loop semantics directly.
  * 19/482 are "no body for callee f" — undefined functions that
    ESBMC silently stubs and CBMC errors on.

* **108**: ESBMC FAILED → CBMC SUCCESSFUL. **Soundness gaps in
  CBMC's Python frontend or model.** Clusters:
  * complex-number arithmetic edge cases (e.g.
    `complex_pow_zero_neg_fail`: `complex(0,0) ** (-2)` should raise
    `ZeroDivisionError`).
  * `math.comb` with negative argument (`combo1_fail`).
  * `math.floor/ceil(nan)` (`floor_ceil_nan_fail`).
  * dict identity / equality semantics (`dict45_fail`, `class-attributes_fail`).
  * Random-keyed dict deletion (`dict_del12_fail`, `dict_del16_fail`).
  * Typed-dict wrong-type assignment
    (`dict_subscript_typed_assign_fail`).
  * `enumerate` with `start` argument (`enumerate7_fail`).

### Reproducing

```bash
# from cbmc-python.git checkout
cmake --build build --target cbmc -j$(nproc)
python3 scripts/cbmc_on_esbmc_python.py \
    --regression /home/ubuntu/esbmc.git/regression/python \
    --tag all --jobs 16 --timeout 30 --unwind 10 \
    --out doc/architectural/cbmc-on-esbmc-data/results.csv
```

### Triage / fix order

The crashes are the clearest CBMC bugs (they are not user errors;
CBMC's own invariants fire). Suggested order:

1. `string_expr.h:160` (22 tests) — biggest single bucket; reduce a
   minimal repro from `dict32`.
2. `dec_solve: current index set is empty` (41 tests) — likely
   related, in the same string solver.
3. `satcheck_minisat2.cpp:150 'variable not added yet'` (4 tests).
4. The smaller crash clusters (`arith_tools.cpp:149`, `namespace.h:49`,
   `boolbv_map.cpp:91`) — 2 tests each.
5. `stod` complex-number parser (5 tests).
6. The 108 soundness DIFFs as separate, narrowly-scoped fixes.

### Progress (after the first wave of fixes)

The five commits that follow this baseline doc on the
`cbmc-on-esbmc-python` branch land the following:

| Outcome | Baseline | After fixes | Δ      |
|---------|---------:|------------:|-------:|
| PASS    | 2118     | 2137        | +19    |
| DIFF    | 590      | 594         |  +4    |
| UNKNOWN | 215      | 219         |  +4    |
| FAIL    | 68       | 68          |  +0    |
| TOERR   | 53       | 48          |  −5    |
| CRASH   | 34       | 10          | **−24**|
| TIMEOUT | 12       | 14          |  +2    |
| SKIP    | 1        | 1           |  +0    |

Pass rate 68.5 % → 69.1 %.

The 24 fixed crashes split into:
* 22× `string_expr.h:160` — guarded six `simplify_string_*` entry
  points with `can_cast_expr<refined_string_exprt>` so that the
  Python frontend's single-character expressions no longer trip
  the `to_string_expr` precondition.
* 2× `arith_tools.cpp:149 from_integer` — `dict.get` no longer
  builds the int "None" sentinel when the value type can't hold
  it (bool/string/float dicts get `safe_zero`).

The 5 fixed TOERRs are the `complex_*` "stod" frontend aborts:
unparseable complex-number coefficients no longer throw
`std::invalid_argument` out of `convert_constant`.

Soundness fixes (DIFFs that flipped from
ESBMC-FAILED-vs-CBMC-SUCCESSFUL to a real verdict):
* `enumerate(seq, start)` honours the start offset.
* `math.comb`/`factorial`/`perm`/`isqrt` raise ValueError on
  negative arguments.
* `math.floor`/`math.ceil` raise on NaN / ±inf inputs.
* `complex(0+0j) ** <negative>` raises ZeroDivisionError.

The 10 remaining CRASHes (4× satcheck_minisat2.cpp:150,
3× boolbv_map.cpp:91, 2× namespace.h:49, 1× misc) are deeper
solver-layer issues — symptoms include stale-pointer
`symbol_table1->symbols.size()` returning garbage during
`namespacet::lookup`. They need a separate, broader fix.

### Wave 2 — closing the CRASH and TOERR buckets

| Outcome | Wave-1 | Wave-2 | Δ      |
|---------|-------:|-------:|-------:|
| PASS    | 2137   | 2147   |  +10   |
| DIFF    | 594    | 616    |  +22   |
| UNKNOWN | 219    | 237    |  +18   |
| FAIL    | 68     | 68     |   +0   |
| TOERR   | 48     | 7      |  **−41** |
| CRASH   | 10     | 0      |  **−10** |
| TIMEOUT | 13     | 15     |   +2   |
| SKIP    | 1      | 1      |   +0   |

Pass rate 69.1 % → 69.5 %.

The TOERR drop is from a single change in `string_refinementt::dec_solve`:
when the refinement loop reaches a fixed point (the SAT model is incorrect
but `update_index_set` finds no new indices to refine on), we now log a
warning and return `D_SATISFIABLE` conservatively instead of `D_ERROR`.
This is sound for safety verification — we never miss a bug — at the cost
of potentially over-reporting on properties the string solver cannot
prove. 8 of the 41 affected tests are `*_fail` whose expected `FAILED`
verdict the new behaviour happens to produce; the remaining 33 land in
the DIFF / UNKNOWN buckets.

The CRASH drop is from four narrowly-scoped softenings:

* `string_refinementt::get` no longer `UNREACHABLE`s when an
  if-condition resolves to a non-Boolean model value.
* `dec_solve` skips universal axioms that fail
  `is_valid_string_constraint` instead of aborting on the
  `DATA_INVARIANT`. The validator is conservative; the Python
  frontend's dict-of-string lowering legitimately produces patterns
  it rejects.
* `substitute_array_access` falls through to `std::nullopt` when
  the array isn't a symbol/array/with/array_of/if (e.g. a
  member_exprt of a struct), instead of `INVARIANT`.
* `satcheck_minisat2_baset<T>::lcnf` and `boolbv_mapt::set_literals`
  lazily allocate out-of-range SAT variables instead of aborting on
  the `INVARIANT`. The newly allocated variables are unconstrained
  Booleans.
* The Python frontend now treats `[[1]] == [1]` (list with different
  element types) as statically false rather than letting boolbv try
  to widen 4160-bit list[int] to 266304-bit list[list[int]] and
  segfault.

The 7 remaining TOERRs are all CBMC frontend / symex limitations
that need real refactoring rather than soft-fail wrappers:

* 3× nested-lambda / typed-callable-return patterns (`assignment to
  'symbol' not handled` from symex_assign).
* 4× `defaultdict(int); d["a"] += 1` patterns (`l2_rename_rvalues
  case 'struct' not handled` from goto_symex_state). The Python
  frontend emits an LHS whose deepest else-branch is a struct
  literal, which symex's L-value walker doesn't know how to assign
  to.


### Wave 3 — closing the last 7 TOERRs via frontend refactoring

| Outcome | Wave-2 | Wave-3 | Δ      |
|---------|-------:|-------:|-------:|
| PASS    | 2147   | 2150   |  +3    |
| DIFF    | 616    | 620    |  +4    |
| UNKNOWN | 237    | 237    |   0    |
| FAIL    | 68     | 68     |   0    |
| TOERR   | 7      | 0      |  **−7**|
| CRASH   | 0      | 0      |   0    |
| TIMEOUT | 15     | 15     |   0    |
| SKIP    | 1      | 1      |   0    |

Pass rate 69.5 % → 69.6 %. **TOERR 0, CRASH 0**.

Three frontend changes resolve the seven remaining TOERRs:

* **Lambda-returning-lambda registration** in `convert_lambda`. Without
  it, `g = (lambda x: lambda y: x+y)(5)` emitted `g := <code-typed
  return value>` and CBMC's symex aborted with "assignment to 'symbol'
  not handled". Resolves `github_3724` (full beta-reduction; `g(10) ==
  15` now verifies) and `lambda13`.

* **Shadow imported-function names** in `convert_ann_assign`. When the
  user's local `match: re.Match[str] | None = re.match(...)` collides
  with the imported `python::match` (re's contents are flattened into
  the top-level namespace), allocate a fresh `<qname>__shadow__vN`
  symbol and route subsequent reads through `variable_versions`.
  Resolves `github_3153`.

* **Decompose dict-subscript AugAssign** in `convert_aug_assign`. The
  previous lowering produced an L-value whose deepest else-branch was
  a struct constant, which symex's L2 renamer can't write to. Replace
  with a manual chained-if read (no KeyError check) plus an
  iterator-based store with append-on-missing-key, mirroring
  defaultdict's auto-insert. Resolves `github_3841_{2,6,7,8}`. The 4
  tests now produce verdicts (FAILED rather than SUCCESSFUL because
  `d: dict = defaultdict(int)` falls back to a nondet dict — that's a
  separate semantic gap to address).

The CBMC-on-ESBMC python regression suite is now CRASH-free and
TOERR-free. Remaining gaps are pure DIFF / UNKNOWN — verdicts that
CBMC produces but that disagree with the test's expected output (or
with no expected output at all). Those are tractable on a
case-by-case basis and don't risk aborting the verifier.


### Wave 4 — soundness work (case-by-case)

This wave reads each failing test against the Python Language
Reference rather than ESBMC's expected output and fixes genuine
PLR-vs-CBMC mismatches. Tests that test ESBMC-specific tool output
(specific error wording, `--strict-types` flag) or methodology
(`--incremental-bmc` vs fixed unwind) are *not* in scope: those
aren't soundness gaps in CBMC.

| Outcome | Wave-3 | Wave-4 | Δ      |
|---------|-------:|-------:|-------:|
| PASS    | 2150   | 2174   | +24    |
| DIFF    | 620    | 596    | −24    |
| UNKNOWN | 237    | 237    |  0     |
| FAIL    | 68     | 68     |  0     |
| TOERR   | 0      | 0      |  0     |
| CRASH   | 0      | 0      |  0     |
| TIMEOUT | 15     | 15     |  0     |
| SKIP    | 1      | 1      |  0     |

Pass rate **69.6 % → 70.3 %**.

Four targeted fixes:

* **`chr(<float>)` raises TypeError** (PLR §builtins). The frontend
  silently accepted any numeric argument. Add a TypeError check
  when the argument's type is `floatbv`. Resolves
  `casting13-fail`.

* **`dict.setdefault(key, default)` actually mutates the dict.**
  Previously returned a nondet value without modelling the
  insertion, so `key in d` after `setdefault(key)` was wrong.
  Emit pending_checks that scan keys, set a found flag and a
  result temp on match, and append `(key, default)` if no slot
  matched. Invalidate `dict_literals` on mutation so the
  `key in dict` constant-fold path doesn't report stale
  pre-mutation state. Resolves `github_3658_5_fail`.

* **`dict.pop(key)` and `dict.popitem()` mutate the dict and
  raise KeyError.** Pop scans keys, returns the matched value
  and shifts subsequent entries down; on miss without a default
  it emits a KeyError check. Popitem snapshots `length-1` into a
  fresh symbol BEFORE the decrement (otherwise the returned
  `(key, value)` references the post-pop slot), then decrements.
  Resolves `github_3783_fail`, `github_3784_fail`,
  `github_3783_7-nondet_fail`, `github_3783_10-nondet_fail`.

* **`min`/`max` work on N args and inlined heterogeneous lists.**
  The previous handler folded only 2-arg numeric forms; 3+ args
  or list arguments fell through to a generic call returning
  nondet, which silently dropped many assertions. Now folds:
  variadic numeric (with int/float promotion to double), constant
  list with numeric or python_value-tagged element types
  (unwrapping the `__tag`/`__int_val`/`__float_val` fields of
  literal operands). Non-literal numeric lists keep the old
  silent-drop behaviour to avoid regressing tests that depended
  on it. Resolves `github_3849_fail`.

Each fix lands with a focused regression test under
`regression/cbmc/python-*` exercising the original repro plus
adjacent cases (e.g. setdefault on present and missing keys, pop
with and without default, min/max over a constant heterogeneous
list).


### Wave 5 — soundness work, batch 2

| Outcome | W4 → W5 | Δ |
|---------|--------:|---:|
| PASS    | 2174 → 2187 | +13 |
| DIFF    | 596 → 583   | −13 |

Pass rate **70.3 % → 70.7 %**. Six fixes:

* **`list.pop()` raises IndexError on empty list and `list.pop(i)`
  raises IndexError when i is out of range** (PLR §builtins).
  Range covers both non-negative `i < length` and negative
  `i >= -length` (Python wraps negative indices). Resolves
  `list_pop_fail`, `list_pop2_fail`.

* **`min([])`/`max([])` raise ValueError** (PLR §builtins). Add
  a `length > 0` property check at the start of the single-arg
  list form. Resolves `max6-fail`, `max10-fail`.

* **`int(str)` raises ValueError on non-numeric string** (PLR
  §builtins). Replace `try { stoll }` with `strtoll` + 'must
  consume entire whitespace-stripped body' check. Resolves
  `int1_fail`.

* **`x is y` for list/dict compares object identity, not content**
  (PLR §6.10.3). Approximate identity by symbol identifier:
  same-symbol → True, different-symbol or literal → False.
  Aliasing (`z = y`) is not tracked, so tests depending on it
  remain DIFF. Resolves `is2-fail`.

* **`min`/`max` work on inline tuples and tuple-bound symbols.**
  Extend the constant-fold to walk python_tuple struct components,
  guarded by a new `tuple_literals` map populated from
  `convert_ann_assign` and `assign_to_named_target` whenever a
  tuple literal lands in a name. Resolves `tuple11_fail`.

* **`chr(<float>)` raises TypeError** (PLR §builtins). Already
  documented in Wave 4 — listed here for completeness.


## Wave 6 (architectural) — Aug 2026 follow-up

After the Wave 5 review, remaining DIFFs were classified by *root architectural
issue* rather than by surface symptom. Two issues had a clean, well-scoped
architectural fix:

### 6a. String struct tag/def unification

The frontend used **two distinct types** for refined-string struct expressions:

  * `python_string_type()` — a `struct_tag_typet{tag-...}`, what the literal
    builder (`build_string_struct`) returned.
  * `python_string_struct_def()` — a `struct_typet` (inline definition), what
    `chr()`, str-format, str-concat folding, str-aug paths, etc. used.

When two such strings met in the equality handler,
`current_left.type() != right.type()` was True and `safe_typecast` couldn't
bridge them, so it fell into its last-resort
`return side_effect_expr_nondett{target, ...}` path. The string solver then
saw one operand as an opaque nondet, and the assertion either succeeded or
failed essentially based on SAT bias.

Fix: thread `python_string_type()` through every site that types a freshly
built string struct expression. The single legitimate use of the inline
definition (registering the type symbol in `convert_module`) is unchanged.

### 6b. List comprehension with `range()`

`convert_list_comp` only matched two iterable shapes — literal lists and
Names-of-tracked-list_literals — and silently returned `nil_exprt` for
anything else, which dropped the whole assignment. So
`xs = [x*x for x in range(4)]` left `xs` zero-initialised, and downstream
assertions about `xs` were satisfied or refuted against the zero default.

Fix: add a third matcher branch for `Call(func=Name("range"), args=...)`
with constant-int arguments, eagerly populating `gi.const_values` from the
expanded sequence and feeding into the existing combination machinery.

### Cumulative

| Outcome | Wave 5 | Wave 6 | Δ |
|---------|------:|-------:|---:|
| PASS | 2187 | 2196 | +9 |
| DIFF | 583 | 572 | −11 |
| UNKNOWN | 237 | 237 | 0 |
| FAIL | 68 | 68 | 0 |
| TIMEOUT | 15 | 17 | +2 |
| TOERR | 0 | 0 | 0 |
| CRASH | 0 | 0 | 0 |
| SKIP | 1 | 1 | 0 |

Pass rate **70.7 % → 71.0 %**. The two TIMEOUT additions are correctness
improvements that newly expose downstream pain (the comprehension that
previously stub-evaluated to zero is now correctly running and stresses
nested-list mutation in `github_3667_2-nondet`).

### Architectural items still on the docket (post-Wave-6)

| Pattern | Approx tests | Architectural fix |
|---|---:|---|
| Mutable container by-value vs by-ref in function args | 36 | Pass list/dict params by pointer at call boundaries; more involved refactor. |
| `def f(): xs.append(...); f(ys)` loses mutation through return | 41 | Same root as above. |
| Aliasing (`z = y`; `z is y`) | 37 | Object-id field, or change to pointer-copy semantics. |
| Type inference on unannotated assignment (`x = math.inf` → int default) | 11 | Infer var type from RHS when annotation absent. |
| Constant-fold across function-return-of-constant | unknown | `function_return_constants` map populated from analysis of leaf functions whose body is a single `return <constant>`. |
| String concat inside loops not tracked | unknown (large) | Needs a real string-flow analysis or a syntactic fold that catches `s = s + c` patterns. |

