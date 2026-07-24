# Python frontend — upstreaming PR-series plan

Status: DRAFT (2026-07-23), after the reconciliation onto `origin/develop`
(merge `9511522f74b`, branch `cbmc-on-esbmc-python`). No PRs pushed yet.

## Guiding principle (user directive)

Stack and land **all non-core-Python changes first** — everything outside
`src/python` and `regression/python*` — and get them merged **before** any
Python frontend change lands. Caveats: some changes that physically sit outside
`src/python` are nonetheless **Python-coupled** and should travel *with* the
Python PRs rather than land first (notably parts of `util/irep_ids.def` and the
`--python-*` `cbmc_parse_options` entries). Judgement is applied per change
below.

## Method

The net delta vs `origin/develop` (`git diff origin/develop..HEAD`) restricted
to non-Python-directory paths was enumerated and each change read and
classified into three buckets:

- **A — generic & independently upstreamable** (land first, on their own merit).
- **B — Python-coupled** (ship *with* the Python PRs even though outside
  `src/python`).
- **C — invariant-weakening / defensive core patches** (driven by Python's
  dynamic typing; each either needs a principled rework/root-cause before
  upstream will accept it, or must ship with the Python work with explicit
  justification). These must NOT masquerade as clean independent core PRs.

## Bucket A — land first (generic, independently upstreamable)

Ordered roughly by dependency / reviewability. Each should build and pass tests
on `develop` with no Python code present.

1. **IEEE sign-aware expression builders** — `src/util/ieee_float.{h,cpp}` +
   `unit/util/ieee_float.cpp`. Adds `ieee_signbit` / `ieee_fabs` /
   `ieee_copysign` built on `sign_exprt` (correct negative-zero semantics),
   with a 124-assertion unit test. Fully generic; no Python dependency. Best
   first PR.
2. **`irept::compare` O(1) shared-instance fast path** — `src/util/irep.cpp`.
   Under `SHARING`, returns 0 immediately when `data == i.data`. Generic
   performance win for any `std::map<exprt>`-heavy path. Standalone.
3. **Do not slice string-refinement intrinsics** — `src/goto-symex/slice.cpp`
   (`contains_string_refinement_intrinsic`). The `cprover_string_*` /
   `cprover_char_*` / `cprover_associate_*` applications carry non-data side
   effects into the string solver (e.g. `associate_array_to_pointer`), so
   data-dependency slicing of them is unsound. This is a **generic**
   string-refinement soundness fix (applies to JBMC strings too), not
   Python-specific. Verify against `jbmc-strings` before proposing.
4. **`--overflow-check` convenience alias** — `src/ansi-c/goto-conversion/
   goto_check_c.h`. Enables signed+unsigned overflow checks together. Generic
   CLI ergonomics.
5. **`--no-slice-formula` option** — `src/goto-checker/bmc_util.h` (+ the
   `slice.cpp` wiring). Generic CLI flag; pairs naturally with PR 3.
6. **Robustness of string simplifiers** — `src/util/simplify_expr.cpp`
   (`simplify_string_endswith` / `contains` / `is_empty` bail out with
   `unchanged` when arguments are not `refined_string_exprt`). Generic
   defensive-but-benign guard (returns unchanged rather than misapplying). Can
   land first; frame as hardening.

Drop (do not PR): `src/util/union_find_replace.cpp` — whitespace-only churn.

## Bucket C — invariant-weakening / defensive (rework or ship-with-Python)

These are all symptoms of feeding CBMC core dynamically-typed (Python) values.
Upstream reviewers will (rightly) scrutinise anything that weakens an
`INVARIANT` / `DATA_INVARIANT` / `CHECK_RETURN`. Two sub-options per item:
root-cause into a principled fix, or ship with the Python series behind clear
justification (and, where possible, gate to `language_mode == "python"`).

- `src/solvers/sat/satcheck_minisat2.cpp` — replaces the "variable not added
  yet" INVARIANT with lazy `newVar()`. **Root-cause first:** out-of-order
  literal allocation from the wide-struct-equality flow is a symptom; a clean
  fix would allocate in order rather than defensively patch the solver layer.
- `src/solvers/flattening/boolbv_map.cpp` — same defensive-allocation pattern
  replacing an INVARIANT. Same root-cause recommendation; likely the same
  underlying cause as the satcheck item.
- `src/util/simplify_expr_struct.cpp` — relaxes the member-type DATA_INVARIANT
  to a `typecast_exprt`. Python dynamic-struct-specific; prefer to ship with
  Python (or make the frontend not produce the mismatched member access).
- `src/solvers/flattening/pointer_logic.cpp` — best-effort fallback when a
  byte offset can't resolve to a sub-object (SMT String). Defensible as
  "counterexample reconstruction is best-effort"; could be a small generic PR
  IF framed as CE-parsing robustness, else ship with Python.
- `src/goto-symex/symex_set_return_value.cpp` — `conditional_cast` of the
  return value to the return-symbol type. Python dynamic-typing driven.
- `src/goto-symex/symex_function_call.cpp` — struct/struct_tag parameter
  typecast (same-layout) instead of `byte_extract`. Python-driven.
- `src/goto-symex/goto_symex.cpp` — `symex_assign` type-mismatch handling and
  the explicitly `language_mode == "python"`-gated
  `resolve_python_string_content` / `associate_array_to_pointer` additions.
  The python-gated parts are **Bucket B** (ship with Python); any
  language-agnostic robustness should be split out and justified.

## Bucket B — ship WITH the Python series (outside `src/python`, but coupled)

- `src/util/irep_ids.def` — the Python-specific ids only (`smt_string`,
  `C_python_string_handle`, `C_python_int_handle`, `cprover_string_smt_*`,
  `cprover_string_re_*`, `chr`/`split`/`repeat`/`compare`/`strip`/
  `index_of_from`). (Upstream's generic `cprover_regex_*` ids are already on
  `develop` — not ours to land.)
- `src/util/std_types.h` — `smt_string_typet`.
- `src/util/arith_tools.cpp`, `src/util/expr_initializer.cpp`,
  `src/util/refined_string_type.h`, `src/solvers/flattening/boolbv_width.cpp`
  — the `ID_smt_string` / struct-tag / tagged-union handling.
- `src/solvers/smt2/smt2_conv.{cpp,h}` — our Python string/regex `convert_expr`
  handler (~1860 lines) + the native-string length bound. (Upstream's generic
  SMT-LIB encoder is already on `develop`; keep our layer scoped to Python ids,
  as the reconciliation did.)
- `src/solvers/strings/python_regex_to_smt.{cpp,h}` + the
  `string_constraint_generator_*` / `string_refinement.cpp` hooks that mention
  the Python frontend. (Split any *generic* string-refinement improvements in
  `string_refinement.cpp` into Bucket A where cleanly separable — needs a
  finer read per hunk.)
- `src/cbmc/cbmc_languages.cpp` — `register_python_language`.
- `src/cbmc/cbmc_parse_options.{cpp,h}` — the `--python-*` options (all of
  them). Per the directive, these are case-by-case, but every `--python-*`
  flag is Python-coupled by definition.
- `scripts/python-fuzz/*`, `scripts/python_fuzzer.py`, `unit/python/*`,
  `unit/solvers/strings/python_regex_to_smt/*`, `regression/cbmc/python-*`.

## Python PR stack (after Buckets A land upstream, and C are resolved)

Then land the frontend itself in reviewable slices, each with its own
`regression/python` tests (rough dependency order):

1. Skeleton: `python_language` + parser/`json` AST ingestion + registration
   (the Bucket-B `cbmc_languages` + minimal `cbmc_parse_options`), typing
   foundation (`python_value` tagged-union, boundary helpers).
2. Core statements/expressions, control flow, functions/calls, classes.
3. Containers (list/dict/set/tuple), comprehensions, the constant-tracking maps.
4. Strings & regex (Bucket-B `smt2_conv` layer + `python_regex_to_smt`),
   `--python-smt-strings`.
5. Exceptions/PLR semantics, the opt-in strict/soundness flags.
6. Docs (`doc/python-*`), the soundness lint, fuzz/oracle scripts.

## Open items before proposing PRs

- **`smt-backend` test tags** (audited 2026-07-23): **55** `regression/python`
  tests hardcode a solver flag (`--cvc5` / `--z3` / `--python-smt-strings`) and
  **all 55 are `CORE` with no `smt-backend` tag**. The CBMC convention is a
  space-separated first-line tag list, e.g. `CORE smt-backend no-new-smt`
  (cf. `regression/cbmc/struct15`). These pass locally (cvc5 + z3 installed),
  so nothing is broken today, but upstream CI selects solver-requiring tests by
  this tag. Add `smt-backend` (and, where a specific solver is hardcoded, the
  matching solver tag) to those 55 `test.desc` first lines. This is a
  prerequisite for the **strings/regex + native** Python regression PR (stage 4
  of the Python stack, which lands last), not a blocker for the Bucket-A
  land-first work. The list is reproducible via:
  `grep -rl -- '--cvc5\|--z3\|python-smt-strings' regression/python/*/test.desc`.
- **Finer split of `string_refinement.cpp`**: separate genuinely-generic
  string-solver improvements (Bucket A) from Python-frontend hooks (Bucket B),
  hunk by hunk.
- **Root-cause the two INVARIANT relaxations** (satcheck / boolbv_map) before
  offering them — they likely share one cause in the wide-struct-equality
  bit-blasting path.

## Bucket-C "kept" items — architectural root-cause investigation (2026-07-24)

The two Bucket-C items retained as "needed" (commits 5 and 6) were interrogated
in depth: are they fixing the problem at the right place, or recovering in
shared CBMC core from an ill-typed expression produced by the Python frontend?
Method: instrument each core site to capture the exact type mismatch + origin,
then trace to the frontend construction. **Finding: all four are band-aids in
shared core for frontend-produced ill-typed expressions.** Map + status:

| Core band-aid | Trigger(s) | Frontend root cause | Status |
|---|---|---|---|
| `simplify_member` DATA_INVARIANT relax | `ord_split_element*` | `ord()` built an ARRAY-typed `.data` member access over the string struct's POINTER `data` field | **FIXED at source** (`ord` reads `*(data+0)`); band-aid removed; `ord_split_element` DIFF→PASS |
| `symex_function_call` struct/struct_tag typecast | `plain-missing-attr-nofp` (`__getattr__`), **and** `callable_instance`, `contains_dunder`, `github_*_setitem`, `github_4572` (`__call__`/`__contains__`/`__setitem__`) | synthetic dunder-dispatch call sites pass UNBOXED args (refined_string / scalar) to python_value parameters | **PARTIAL**: `__getattr__` name now boxed (`wrap_value`); band-aid RETAINED for the other dispatch sites (removing it regresses them to TOERR). Full fix = box args at every dunder-dispatch site |
| `symex_set_return_value` conditional_cast | `dunder-iter-next-setitem` (`Range3.__next__`: signedbv→struct_tag), `crash-return-type-mismatch` (`Service.get_items`: struct→struct) | the function's code_typet return type (→ return symbol = python_value) disagrees with `convert_return`'s annotation-driven coercion target (`-> int`), so the body returns an int the symbol can't hold | **band-aid RETAINED** (root fix = make the return-symbol type and the coercion target consistent; box the returned value to the code_typet return type) |
| `symex_assign` type-mismatch conditional_cast | `lambda-object-param-dispatch` (python_value→python_class_P: an UNBOX), `python-library-wave9` (struct vs struct_tag of the same class: representational) | assignment sites don't unbox a python_value to the concrete class lvalue / don't normalise struct vs struct_tag | **band-aid RETAINED** (root fix = unwrap/coerce rhs to the lvalue type at the frontend assignment; normalise struct/struct_tag) |
| `resolve_python_string_content` (goto_symex Part B) | `string-produced-subscript` (+ others) | — | **KEEP**: a legitimate, gated (language==python) string-backend materialisation hook, not an invariant relaxation |

Landed root-cause fixes (on `cbmc-on-esbmc-python`): `b554fba5c5c` (ord) and
`91ace9fc012` (getattr name boxing). Full `regression/python` green; ESBMC
sweep 0 regressions + 1 improvement (`ord_split_element` DIFF→PASS).

Lesson recorded: the `symex_function_call` band-aid covers a WHOLE CLASS of
dunder-dispatch sites; a per-site fix must be validated against the ESBMC sweep
(not just `regression/python`) before removing the band-aid. The remaining
root-cause fixes (dunder-arg boxing across all dispatch sites; return-symbol
type consistency; assignment unbox/normalise) are the correct next steps to
retire commits 5 and 6 entirely; each needs the full suite + sweep gate.
`upstreaming-linear` must be re-derived once these land (its commits 5/6 still
carry the pre-investigation band-aids).

## Bucket-C follow-up: whole-group frontend fixes (2026-07-24, session 2)

**Group 1 — dunder-dispatch unboxed args (symex_function_call band-aid): DONE
(`7df818d567b`).** Root cause: synthetic dunder/protocol dispatch calls
(__call__, __contains__, __setitem__, ...) bypass convert_call's argument
coercion and hand unboxed values (int, python_tuple) to python_value
parameters; goto-symex then byte-extracted/typecast them into the python_value
slot (a byte reinterpret that yields a garbage __tag — a latent PLR soundness
hole). Whole-group fix: a shared `coerce_call_args(callee_type, args)` helper
(mirrors convert_call: wrap_value into a python_value param, else
safe_typecast) applied at the dispatch sites; the goto-symex struct/struct_tag
typecast is removed. Full suite green; ESBMC sweep 0 regressions + 3
improvements (callable_instance / contains_dunder / ord_split_element DIFF→PASS).

**Groups 2 & 3 — return-value + assignment desync (symex_set_return_value /
symex_assign band-aids): DIAGNOSED as a shared architectural issue, deferred.**
These are NOT independent mis-typed sites; they share one root: **a value's
symbol type is finalised/widened by a post-conversion pass AFTER the body
expression was already emitted and coerced**, leaving the body expression
desynced from the final symbol type. Instrumented evidence:
- `Range3.__next__`: at `convert_return`, `ret_val` is python_value while the
  declared return is `int` (`-> int`); `convert_return` (control.cpp ~2443)
  then WIDENS the function's return type to python_value to match — but with
  `__next__`'s multiple exits (`return v` + `raise StopIteration`) the passes
  leave one exit typed differently from the finalised return symbol, which
  symex_set_return_value then casts (int↔python_value — a reinterpret, not a
  box: the concerning case).
- `Service.get_items -> List[Dict[str,Any]]`: returns `response["Items"]` off an
  `object`; the returned struct's tag differs from the annotation's list struct
  (a genuine dynamic-typing return), handled today by the symex cast.
- `lambda-object-param-dispatch`: assigns a python_value to a
  `python_class_P`-typed lvalue (needs an UNBOX / unwrap_value, not a
  reinterpret typecast).
- `python-library-wave9`: assigns a `struct` to a `struct_tag` of the SAME
  class (a benign representational mismatch; a follow-tag normalisation, and
  the current cast is sound for this one).

The architecturally correct whole-group fix is a **type-finalisation change**:
either finalise return/variable symbol types BEFORE converting the body (so
`convert_return` / the assignment path coerce to the final type), or run a
single post-conversion pass that re-coerces every `return` operand and
assignment RHS to its final symbol type via the boundary helpers (wrap_value /
unwrap_value / safe_typecast). Rushing per-site coercions here is unsafe for
PLR (a wrong box-vs-reinterpret-vs-unbox choice is exactly how a false proof is
introduced), so the two symex band-aids are RETAINED pending that refactor.
Both are triggered by only 2 tests each; the int↔python_value and
python_value↔class casts are the ones to convert to real box/unbox once the
type-finalisation ordering is fixed.

## Type-finalisation refactor — DONE (2026-07-24, session 3, `6434ae08cac`)

Retired the last two Bucket-C band-aids (symex_assign type-mismatch cast +
symex_set_return_value cast). Investigated both candidate designs:

- **Approach A — finalise symbol types BEFORE body conversion.** Rejected: a
  massive reorder of the conversion driver (return types are widened by
  mid-conversion logic in convert_return and post-conversion re-inference), AND
  it would not even remove the value-flow coercions — e.g. a python_value read
  into a concrete-class slot needs an actual UNBOX regardless of when the type
  is known.
- **Approach B — a post-conversion re-coercion pass.** Chosen. `convert()` ends
  with `finalise_slot_types()`, which walks every ID_code body and coerces each
  assignment RHS to its LHS type and each return value to the function's return
  type via `safe_typecast` (unwrap python_value→concrete, wrap
  concrete→python_value, sound scalar/struct casts otherwise). This is the
  frontend, Python-semantic-correct equivalent of the central symex chokepoint
  the band-aids occupied, and it is whole-group (covers all assignments/returns,
  present and future).

Key implementation note: the pass MUST run at the frontend codet level at the
end of `convert()` (after all type widening) — an equivalent coercion placed
earlier, or a blanket cast in goto-symex, is wrong. `safe_typecast`'s box/unbox
is what makes it PLR-sound (the band-aids' raw typecast reinterpreted bytes).

Validated: the three trigger tests (dunder-iter-next-setitem,
lambda-object-param-dispatch, python-library-wave9) pass with BOTH band-aids
removed; full regression/python green; ESBMC sweep 0 regressions; oracle 0-NEW;
both fuzz gates OK.

**Bucket-C is now fully retired**: SAT-literal + pointer_logic patches deleted
as dead code (invariants restored); ord ill-typed access fixed; __getattr__ and
all dunder-dispatch args boxed (coerce_call_args); assignment/return desync
fixed (finalise_slot_types). No goto-symex/core invariant relaxation from the
Bucket-C set remains. `upstreaming-linear` must be re-derived to fold in this
session's frontend fixes.

## upstreaming-linear RE-DERIVED (2026-07-24, session 3)

After Bucket-C was fully retired, `upstreaming-linear` was rebuilt from
`origin/develop` (content-based, guaranteeing tree convergence to
`cbmc-on-esbmc-python`). It is now **9 commits** (down from 13 — the two
Bucket-C core commits are gone, since those band-aid files are pristine
upstream again):

1. util/irep: O(1) SHARING fast-path for irept::compare        (Bucket A)
2. util: IEEE-754 sign-aware expression builders               (Bucket A)
3. goto-symex/slice: don't slice string-refinement intrinsics  (Bucket A)
4. util/simplify_expr: guard string simplifiers                (Bucket A)
5. solvers/util: Python SMT-LIB string/regex backend + types   (Bucket B)
6. cbmc/goto-symex: string-content hook, registration, CLI     (Bucket B)
7. python: the front-end (incl. ord/getattr/coerce_call_args/finalise_slot_types)
8. python: regression + unit + fuzz + lint + sweep tooling
9. doc + wiring: architecture/plans, stdlib harness, CI, gitignore, registration

Verified: Bucket A (1–4) builds `cbmc`+`unit` standalone on `origin/develop`
(`[ieee_float]` 124 assertions); the full branch builds; tree is identical to
`cbmc-on-esbmc-python` except the intentionally-dropped `union_find_replace.cpp`
whitespace. No Bucket-C core band-aid remains in the history. Bucket-A PR
branches (`python-upstream-01..04`) are unchanged and still valid (those four
files did not change this session).

## Bucket-B scrutiny — the wider-use (generic / JBMC) angle (2026-07-24, session 4)

Applied Bucket-C rigor to the non-src/python changes, plus the opposite angle:
is each change actually of WIDER use (generic CBMC / JBMC-Java) rather than
Python-only? The `src/solvers/strings/*` refined-string solver is SHARED with
JBMC, so it is the prime candidate.

**Finding: a large share of the `src/solvers/strings/*` changes are GENERIC
string-solver bug fixes of wider use — Python merely EXPOSED them.** Reframe
these as generic, land-first improvements (not Python-coupled). Validated
watertight and JBMC-/C-safe:

- **`cannot_be_neg` namespace fix** (`string_constraint.{cpp,h}` + the `ns`
  plumbing through `string_constraint_generator_{comparison,indexof,testing}`,
  `string_format_builtin_function`, `string_builtin_function.h`): the bound
  non-negativity check built a **fresh EMPTY `symbol_tablet`/`namespacet`** and
  solved against it — objectively wrong (symbol/type lookups during the check
  see nothing). Thread the real `ns` through. Generic correctness fix.
- **`get_string_expr` robustness** (`string_dependencies.cpp`): replaced
  `expr_checked_cast<struct_exprt>` + `of_argument` (crashes on a non-struct
  refined-string) with `get_string_expr`. Generic robustness.
- **handle()-path string-builtin axioms** (`string_refinement.cpp`
  `convert_rest`/`set_to` override + `is_cprover_string_application` over the
  FULL generic `cprover_string_*` id set incl. Java-only ids like `of_long`,
  `code_point_at`, `trim`): interpreted string built-ins consumed via CBMC's
  `handle()` path (a boolean/value context) never reached `set_to()`, so their
  axioms were never generated. Generic solver-pipeline fix.

**Validation (watertight, all green):**
- jbmc-strings regression: **all successful** (87 skipped) — built jbmc from
  this branch (`WITH_JBMC=ON`, build-jbmc/). The shared changes do NOT regress
  Java string verification.
- regression/strings (C, refined-string): all successful (21 skipped).
- `[strings]` unit: 388 assertions pass.
- Python: full suite green, sweep 0 regressions.

**Genuinely Python-specific (stay Bucket B):** `add_axioms_for_python_strip`
(Python `strip` ≠ Java `trim`; `string_constraint_generator_transformation` +
`_main` + `string_constraint_generator.h` decl); the `smt2_conv` Python
`re.*`/`smt_*` handler; `python_regex_to_smt`; `smt_string_typet`
(`std_types.h`) + `smt_string` irep ids + `boolbv_width` smt_string + the
`arith_tools`/`expr_initializer` smt_string/struct bits; `register_python_
language`; `--python-*` options; `goto_symex` `resolve_python_string_content`
(Part B, python-gated). `refined_string_type.h` struct_tag recognition is
generic-safe robustness (Python-motivated; Java's struct form still matches).

**Still to finish:** a hunk-level split of `string_constraint_generator_main.cpp`
(mixes generic `ns` threading + the interpreted-ids sync with the python_strip
dispatch) and confirmation that `string_refinement.cpp`'s 4 python touches are
separable from its generic body.

## Refined Bucket-B classification (2026-07-24, session 4, task 3 complete)

### (A) GENERIC / wider-use — reframe as LAND-FIRST (JBMC-safe: jbmc-strings + regression/strings + [strings] unit + Python all green)
- `string_constraint.{cpp,h}` + `ns` plumbing (`string_constraint_generator_{comparison,indexof,testing}`, `string_format_builtin_function.cpp`, `string_builtin_function.h`) — `cannot_be_neg` empty-namespace bug fix.
- `string_dependencies.cpp` — `get_string_expr` robustness (drop `expr_checked_cast<struct_exprt>`).
- `string_refinement.cpp` — handle()-path string-builtin dependency-graph fix over the FULL generic `cprover_string_*` id set (a few python touches to hunk-split out).
- `array_pool.cpp` — associate re-insertion robustness for multiply-inlined bodies (generic; any frontend).
- `string_builtin_function.cpp`, `string_concatenation_builtin_function.cpp`, `string_insertion_builtin_function.cpp` — `get_string_expr` + `ns` threading.
- `refined_string_type.h` — struct_tag recognition (generic-safe; Java struct form still matches; minor: substring match, prefer exact).
- (already Bucket A) `irep.cpp` SHARING, `ieee_float.{h,cpp}`, `goto-symex/slice.cpp`, `simplify_expr.cpp`.
- CLI extras `--overflow-check`/`--no-slice-formula` (goto_check_c.h, bmc_util.h) — generic, extract from the cbmc_parse_options python bundle.

### (B) BAND-AID → root-cause fix at the frontend + REVERT (Bucket-C-style; NOT stays-Bucket-B)
- `arith_tools.cpp` — `from_integer(int, struct)` relaxes a core `PRECONDITION(false)`. Confirmed a band-aid: frontend callers misuse `from_integer` on struct/python_value types — `from_integer(none_sentinel, python_value)` (custom-descriptor) should be `python_none_value()`, and `from_integer(0, <python_class_*>)` (python_converter.cpp:3579 return-compare; decimal-p3, numeric-model-soundness) should be `safe_zero`/a proper comparison. Needed by 7 tests today; fix the callers, then restore the PRECONDITION. (Also re-audit python_converter.cpp:3400/3443 None-into-struct sites.)

### (C) GENUINELY PYTHON-SPECIFIC — STAYS BUCKET B (ships with the frontend)
- `add_axioms_for_python_strip` (`string_constraint_generator_transformation.cpp` + `_main.cpp` dispatch + `string_constraint_generator.h` decl) — Python `strip` ≠ Java `trim`.
- `smt2_conv.{cpp,h}` — Python `re.*` / `smt_*` convert_expr handler (parts MAY be generalisable as reusable SMT-string helpers — flagged for the generalisation review).
- `python_regex_to_smt.{cpp,h}` — Python-regex-syntax → SMT translator.
- `std_types.h` `smt_string_typet`; `irep_ids.def` python + smt_string ids; `boolbv_width.cpp` smt_string width; `expr_initializer.cpp` smt_string init — the Plan-A native SMT-string sort.
- `goto_symex.{cpp,h}` Part B `resolve_python_string_content` — EXPLICITLY `language_mode=="python"`-gated and documented as HARMFUL for JBMC's refinement-constrained char[]; not generalisable by design.
- `cbmc_languages.cpp` `register_python_language`; `cbmc_parse_options.{cpp,h}` `--python-*` options; `goto_check_c` python-mode allowance.

Next: review (C) for items that are CURRENTLY Python-specific but could be generalised (e.g. the `smt_string` native sort and the `smt2_conv` string helpers as a reusable SMT-LIB-String facility; `python_regex_to_smt` as a generic regex-syntax→RegLan translator).
