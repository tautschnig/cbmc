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

## Category-C generalisation review (2026-07-24, session 4)

For each "stays Bucket B" item: can it be GENERALISED (serve JBMC/other
frontends), or can small frontend PRE-WORK let a generic facility do the rest?
PLR soundness and the whole-group lens applied. Prioritised by value/effort.

### 1. The `smt_string` native sort + `smt2_conv` Python handler — the ONE whole-group opportunity (LARGE, high value)
Architecture today has THREE string paths: (a) refined-string SAT solver
(default Python AND JBMC — the shared generic facility); (b) upstream's new
`cprover_string_*` → SMT-LIB `str.*` lowering in smt2_conv (generic, z3/cvc5);
(c) Python's `smt_string` sort + 7 `smt_*` intrinsics under `--python-smt-strings`
(Plan A). **(b) and (c) are both "native SMT-LIB strings" by different means —
(c) is a Python-PARALLEL reimplementation of (b).**
- **Generalisation:** retire (c) by having the frontend emit standard
  `cprover_string_*` (it already does in default mode; 24 generic ids) and rely
  on (b)'s SMT-LIB lowering under z3/cvc5. Pre-work is frontend-side (drop the
  `smt_*` emission + the `smt_string` sort); the remainder is DONE by the
  generic facility.
- **Gap to fill in the generic facility (WIDER USE — benefits Strata + any
  SMT-LIB-string frontend):** upstream's (b) currently lowers only a subset;
  the 7 `smt_*` ops (smt_strcat/strsub/strreplace/from_int/from_code/to_code/
  re_ws) + `of_int`/`of_double`/`parse_int`/case/strip have no SMT-LIB lowering
  yet. Adding them to smt2_conv's `flat_string_ops`/regex map generalises (b).
- **Effort/risk:** LARGE and carefully-gated — (c) reached Plan-A precision/perf
  that (b) ("less tested") must match before retiring (c). Recommend as a
  dedicated milestone, validated against the native corpus + jbmc-strings.
  Net win: one generic SMT-LIB-string facility instead of a Python-parallel one.

### 2. `add_axioms_for_python_strip` — generalise to a parameterised strip (MODERATE)
It differs from the generic Java `add_axioms_for_trim` in exactly two
parameterisable ways: the whitespace PREDICATE (Python: 0x20 + 0x09–0x0d; Java
trim: ≤0x20) and a SIDE-MODE (both / lstrip / rstrip). So Python strip is a
generalisation of Java trim. **Recommend** a generic
`add_axioms_for_strip(str, res, char_predicate, strip_front, strip_back)` with
Java `trim` = strip(≤0x20, both) and Python whitespace-strip = strip(py_ws,
mode). Wider use (adds lstrip/rstrip + configurable predicate to the shared
solver). Low-moderate effort; touches the Java trim path so gate on
jbmc-strings.

### 3. `python_regex_to_smt` — generalise to a regex-dialect → RegLan translator (MEDIUM)
The translator core (char classes, quantifiers, groups, anchors → SMT RegLan
ops) is largely dialect-agnostic; only the surface syntax + the Python `re.*`
MATCH semantics (match/search/fullmatch/group positions) are Python-specific.
**Recommend** factoring the syntax→RegLan core into a generic facility
parameterised by dialect (Python `re`, Java `Pattern`, …), keeping the Python
`re.*` semantic layer thin on top. Medium effort; genuinely reusable.

### 4. NOT generalisable (keep Python-specific by design)
- `goto_symex` Part B `resolve_python_string_content`: explicitly documented as
  HARMFUL for JBMC's refinement-constrained char[]; language-gated on purpose.
- `register_python_language`, `--python-*` options: they ARE the frontend
  registration/config — inherently Python.
- `smt_string` init/width (`expr_initializer`, `boolbv_width`) + ids: only
  meaningful while (c) exists; they disappear WITH the item-1 retirement.

### Recommendation summary
The highest-value architectural move is **item 1** (retire the parallel
`smt_string` backend by generalising upstream's SMT-LIB-string facility and
having the frontend emit `cprover_string_*`), which is a whole-group win but a
large, precision-gated milestone. **Items 2 and 3** are bounded, genuinely-
reusable generalisations that can land independently (gated on jbmc-strings).
None should be rushed: each touches shared solver code where a wrong axiom is a
soundness risk.

## Category-C generalisations — implementation (2026-07-24, session 5)

- **#2 strip — DONE (`6e4b7310d4c`).** Shared `add_axioms_for_strip(str, res,
  is_strippable, strip_front, strip_back, result_type)`; Java `trim` and Python
  `strip`/`lstrip`/`rstrip` are thin wrappers. Behaviour-preserving; JBMC-safe
  (jbmc-strings green), regression/strings green, [strings] unit 388, Python
  suite green, sweep 0-reg.
- **#3 regex — DONE (`f266c4a017e`).** `regex_char_classest` +
  `ascii_regex_char_classes()`; the translator is now dialect-parameterised
  (ASCII default; a Unicode/other dialect can plug in). Behaviour-preserving;
  Python regex tests + suite + sweep green.
- **Reconciliation-regression fix (`182d62224b3`).** Surfaced while running the
  full unit suite for #3: the develop merge had scoped the shared string ops
  out of upstream's `smt2_conv` encoder, breaking the `smt2_convt string and
  regex operator lowering` unit test. Restored them gated on SMT-LIB-native
  operands (Python's struct/smt_string operands skip to the Python handler);
  added the `str.indexof` default-offset special. Unit 57 assertions pass;
  jbmc-strings/regression-strings/[strings]/python-suite/sweep all green. This
  makes the generic SMT-LIB-string facility correct again — the foundation for
  #1.

### #1 (retire the parallel `smt_string` backend) — assessment: a large,
precision-gated milestone; NOT to be rushed
The generic SMT-LIB-string facility (upstream's `cprover_string_*` → `str.*`
encoder) is now correct for native operands (the fix above). But retiring
Python's `smt_string` sort means representing native Python strings as the
SMT-LIB `String` sort (ID_string) throughout `python_value` / list / dict /
call boundaries and emitting standard `cprover_string_*` — i.e. the "native
string type-flip" that was ALREADY attempted and abandoned as too hard (the
dropped WIP stash reached 107→37 TOERRs before being superseded by the
`smt_string`-handle Plan A, which then reached full precision/parity). There is
no safe bounded increment: the 7 `smt_*` intrinsics operate on `smt_string`
operands, so retiring any requires the type-migration. Rushing it risks both
PLR soundness (string-encoding changes → potential false proofs) and the
Plan-A precision. **Recommendation:** treat as a dedicated, precision-gated
milestone (native corpus + jbmc-strings + sweep), with the generic-encoder
foundation now in place. Not attempted in this session.

Operational note recorded: the ESBMC sweep MUST be given an absolute `--cbmc`
path (workers cd into test dirs; a relative path yields a bogus all-ERROR
result); and test.pl gives spurious mass-failures if run concurrently with a
build (re-run settled).

## Milestone #1 (retire the parallel smt_string backend) — spike + stage 1 (2026-07-24)

### Whole-group root (confirmed by spike)
The native Python string backend (`--python-smt-strings`, Plan A) is a *parallel*
SMT-LIB-string facility: its own sort (`smt_string_typet`/`ID_smt_string`), its
own value-returning intrinsics (`cprover_string_smt_{strcat,strsub,strreplace,
from_int,from_code,to_code,re_ws}_func`), and its own ~65-site `smt2_conv`
handler branch. It exists because it was built *before* the develop
reconciliation brought upstream's generic `ID_string`→`String` +
`cprover_string_*`→`str.*` encoder into the tree. Every apparent point fix (the
smt2_conv handler, `boolbv_width`/`expr_initializer`/model-extraction
`smt_string` cases, the ~37 frontend `ID_smt_string` sites) is a symptom of that
one root. The convergence lever is small because the frontend already funnels
native string ops through a handful of primitives (`string_concat`/`string_substr`/
`string_equal`/`native_or_member_string_length`/`python_string_literal` +
`native_string_app`).

### Baseline (precision bar)
Native/cvc5 corpus = 50 tests (`grep -l 'python-smt-strings\|cvc5'`), all pass.
Every convergence stage must keep this green (plus jbmc-strings, regression/
strings, [strings]+smt2_conv unit, full regression/python, ESBMC sweep).

### Stage 1 — DONE (`1ea3c156718`): concat
`cprover_string_smt_strcat_func` was a pure duplicate of the generic
`cprover_string_concat_func` → `(str.++ a b)` (identical lowering for native
String operands). Retired it: `string_concat()` + the strip-reconstruction
helper now emit the generic id; the shared encoder lowers a 2-arg concat over
`smt_string` operands (gated so refined/struct + the Python predicate/query
handlers are untouched); `try_extract_string_literal` recovers regex patterns
from the generic 2-arg concat. All gates green, sweep 0-reg.

### Why substr/replace/queries are NOT simple reroutes — the convention split
The remaining ops differ from the generic facility by **calling convention**,
which is the deeper whole-group issue (PLR-correctness-critical; naive routing
would be a bug or unsound):
- **`smt_strsub`** passes **bitvector** `(offset, len)` and wraps with `bv2nat`;
  the generic `cprover_string_substring_func` convention is **native SMT Int**
  `(start, end)` → `(str.substr s a (- b a))`. Different arg type AND different
  3rd-arg meaning.
- **`smt_strreplace`** is `str.replace_all` (Python replaces **all**); the
  generic `cprover_string_replace_func` map entry is `str.replace` (**first**
  only). Routing naively would be **unsound**.
- **Predicate/query ops** (`contains`/`prefix`/`suffix`/`equal`/`length`/
  `index_of`): the Python handler **bit-encodes** results (`(ite (str.…) bv1
  bv0)`, `int2bv` for lengths) because Python `bool`/`int` are bitvectors,
  whereas the generic encoder returns native SMT `Bool`/`Int`.

So the single whole-group root for the *remaining* convergence is:
**Python-native uses fixed-width bitvector ints / bitvector-encoded bools /
replace-all, while the generic encoder uses native SMT Int / Bool / first-
replace.** Closing it is a deliberate stage — either teach the generic encoder
to accept bitvector position args (bv2nat) and bitvector-encode Bool/Int results
under a Python-operand discriminator, or add generic value-returning
`replace_all` / offset-len `substr` facilities (wider-use, benefiting Strata) —
NOT a per-op reroute. concat converged precisely because it has NO convention
difference (String→String, string operands).

### Latent note (pre-existing, flagged)
The `cprover_string_substring_func` entry restored to the generic encoder map in
`182d62224b3` emits a FLAT `(str.substr s a b)`; per the Phase-2 design the
Strata convention is `(start, end)` → `(str.substr s a (- b a))`. It is
unexercised today (Python uses `smt_strsub`; no Strata substring test), but must
be reconciled when substr converges.

## Milestone #1 — convention-reconciliation stage: direction decided (2026-07-24)

The remaining ops differ from the generic encoder by calling convention
(Python bitvector ints / bitvector-encoded bools / replace-all vs the generic
native SMT Int / Bool / first-replace). Two ways to bridge:

- **Direction A (encoder-side):** teach the shared encoder to detect bitvector
  operands / result-types and insert `bv2nat` / `int2bv` / `(ite … bv1 bv0)`
  itself. Rejected: pollutes the shared (Strata-facing) encoder with
  Python-specific bitvector logic.
- **Direction B (boundary typecasts) — CHOSEN, more general.** The front-end
  emits the generic intrinsic with its NATIVE SMT type (String / Int / Bool)
  and wraps it in an ordinary `typecast_exprt` to/from Python's bitvector type.
  The encoder stays pristine (native `str.*` terms, exactly as Strata uses it),
  and CBMC's **existing universal `convert_typecast`** performs every
  conversion. Verified all four directions already exist in `smt2_conv`:
  bitvector→Int `bv2nat` (signed-aware, ~4542-4565); Int→bitvector `int2bv`
  (~4149); Bool→bitvector `(ite src bv1 bv0)` (~4090); bitvector→Bool
  `(not (= src 0))` (~3925). So a converged op is: front-end declares the
  intrinsic natively + typecast; encoder emits the plain `str.*`; typecast
  bridges. The conversions live in ONE boundary layer, reusing universal
  machinery.

**End-to-end validation (length, then reverted):** routing native `len` as
`typecast(cprover_string_length_func(s):Int, i64)` produced *exactly*
`((_ int2bv 64) (str.len s))` — identical to the retired handler branch — and
the native corpus stayed 50/50 once the encoder convergence was gated on the
**result type** (only natively-typed apps route to the generic encoder).

**Whole-group blocker discovered (why length was NOT committed):** unlike
concat (2 contained emitters, fully retired), `length`/`equal`/etc. are emitted
in TWO conventions — the representation-neutral primitives (`native_or_member_
string_length`, `string_equal`, …) AND ~9 raw `emit_string_function(…length…)`
call sites that declare a **signedbv** result. Converging only the primitive
leaves the raw-emitter convention behind, so the Python handler branch cannot
retire — the change becomes net-negative churn (extra gate complexity, nothing
removed). The **prerequisite** (the actual whole-group fix, and the Plan-A
doc's own standing recommendation) is: **funnel ALL native string-op emissions
through the representation-neutral primitives**, so each op has ONE emission
convention. Then Direction-B-converting the primitive converges the op AND
retires its handler branch mechanically. Risk to weigh: several of those raw
emitters are on the refined (default) path (struct operands), where the
primitive returns a direct `.length` member access rather than the
`emit_string_function` intrinsic — equivalent for a refined struct but on the
high-stakes default path, so the funnelling must be validated per-site against
the full refined suite + jbmc + sweep.

**Next concrete step:** the emitter-funnelling refactor (route the raw
`emit_string_function` string-op sites through the primitives), then converge
length/equal/contains/index_of via Direction B one op at a time, each retiring
its handler branch, gated on the full corpus + suites + sweep.

## Milestone #1 — bool-query batch: revealed the convergeability criterion (2026-07-24)

Attempted to converge the bool queries (equal/contains/is_prefix/is_suffix) via
Direction B (make emit_string_bool_function declare a native Bool result +
typecast; add them to the encoder's converged predicate; delete their handler
branches). Native corpus stayed green, BUT deleting the branches regressed two
`--cvc5 --python-unbounded-ints` tests (`int-unbounded-box-*`). Root cause:
those run the **refined** string representation under an **SMT2** backend, where
string `==`/`in` are NOT refined away (no SAT refinement) and reach smt2_conv as
`cprover_string_equal/contains_func` over refined **structs** — the handler
branches are their **sound structural fallback** (length+pointer compare;
sound, may wrong-fail, never wrong-pass). So those branches are shared with the
refined path and cannot be deleted. Reverted the whole bool-query change.

**Convergeability criterion (the whole-group rule).** A per-op Python handler
branch is *retirable now* iff the REFINED path does not emit that intrinsic to
smt2_conv:
- **concat** — retirable: refined concat is the distinct 4-arg ceremonial form
  (kept), native was the 2-arg value form (retired). DONE.
- **length** — retirable: refined length uses a direct `.length` **member
  access** (no intrinsic reaches smt2_conv), native was the intrinsic (retired).
  DONE.
- **equal/contains/is_prefix/is_suffix** — NOT retirable per-op: refined-struct
  under SMT2 emits the intrinsic and relies on the branch as its sound
  structural fallback. Routing only native to the generic encoder would keep
  the branch (no reduction) and merely split native from refined — churn.
- **substr/replace** — same class (refined emits the intrinsic; plus the
  convention/`replace_all` differences noted earlier).

**Consequence — the next lever is the smt_string SORT retirement, not more
per-op work.** The remaining ops converge *naturally* once native leaf strings
stop being the Python-specific `smt_string` sort (`ID_smt_string`) and become
the upstream `string_typet` (`ID_string`): the encoder's `operands_native`
path already accepts `ID_string` (it only excludes struct/struct_tag/smt_string),
so ALL native string ops would route to the generic encoder in one move, and the
Python handler branches would remain solely as the refined-struct SMT2 fallback.
Direction-B result typecasts (validated for length) remain the mechanism for the
Bool/Int result-encoding at the boundary. Note this sort flip is the larger
migration previously assessed (the in-aggregate `strtab` handle machinery is
orthogonal and stays); it should be its own validation-gated effort.

Net this session: concat + length converged and their parallel surface retired
(`smt_strcat` id + branch; length branch); the convergeability criterion above
now tells us which remaining ops are per-op-retirable (none cleanly) vs gated on
the sort retirement (all of them).

## Milestone #1 — smt_string sort retirement: BLOCKED (the sort is a discriminator, not a duplicate) (2026-07-24)

Spiked the sort retirement by renaming `smt_string_typet`→`string_typet` and
`ID_smt_string`→`ID_string` across all ~116 src sites. The rename **builds
clean and the native/cvc5 corpus stays 50/50, the refined suite / C strings /
[strings] unit / jbmc-strings / sweep are all 0-regression** — BUT the
`smt2_convt string and regex operator lowering` unit test **fails 15/30**
(initially missed behind a blank-tail artifact; caught on re-run).

**Root cause — the sort is the Python-vs-Strata discriminator.** The shared
encoder's `operands_native` gate *excludes* the Python-native string sort so
those applications fall through to the Python-specific handler (which
bit-encodes results and lowers the non-generic ops `from_int`/`from_code`/
`to_code`/`re_ws`/`re_group`/`re_sub`/`match`/…). Strata emits its String
operands as the upstream `ID_string`, which the gate treats as *native* → the
generic `str.*` lowering. These are DISTINCT sorts on purpose: the distinctness
is exactly what lets one shared encoder serve both front-ends. The rename
conflates them — `operands_native` then excludes `ID_string`, so Strata's
`ID_string` operands (the unit test) are wrongly routed to the Python handler.
So `smt_string_typet` is **not** a redundant duplicate of `string_typet`; it is
a functional discriminator.

**The dependency is reversed.** The sort cannot be retired *first* to "unlock"
the op convergence; rather, the op convergence (Python fully adopting the
generic convention) must come *first*:
1. Direction-B ALL result-typed native ops (equal/contains/prefix/suffix,
   substr, index_of — native Bool/Int + boundary typecast) so they lower via
   the generic encoder like Strata;
2. keep the Python handler for the genuinely-non-generic ops, dispatched by
   `fn_id` **regardless of operand sort** (so it no longer needs the sort as a
   discriminator);
3. keep the refined-struct branches (the sound SMT2 fallback);
4. ONLY THEN drop the `operands_native` sort exclusion and unify the sort
   (`smt_string`→`string`), since Python and Strata would by then share one
   convention.

But step 1 is the bool-query convergence that is itself blocked (the handler
branches are shared with the refined-struct SMT2 fallback — see the previous
section). So the clean end-state requires either (a) making refined-string ops
under an SMT2 backend always go through `--refine-strings` (so those intrinsics
never reach smt2_conv and the branches become deletable), or (b) a per-front-end
tag on the application (not the sort) to distinguish Python from Strata. Both
are substantial, separate designs.

**Net:** the sort retirement is a real, larger design problem, not a mechanical
rename — the rename is behaviour-preserving for Python but breaks the
Strata/generic contract the shared encoder must also honour. Reverted; the
clean landed wins remain concat + length. The two truly-retirable ops were
retirable precisely because their refined paths don't emit the intrinsic;
everything else is gated on resolving the refined-SMT2-fallback / Python-vs-
Strata-discriminator coupling above.

## Milestone #1 — operand-SORT dispatch landed; sort retirement's last blocker isolated (2026-07-24)

Acting on the principle "the back-end must not branch on which front-end
produced the expression", reworked the encoder's dispatch (commit
`aa420e01e6d`):

- `operands_native` now excludes ONLY refined strings (struct / struct_tag).
  Both the upstream `string_typet` (ID_string, Strata) and the Python native
  sort (ID_smt_string) are the SMT String sort, so both route to the generic
  `str.*` lowering uniformly. The dispatch is on operand SORT (String vs
  refined-struct), never on the front-end. The per-op `smt_string`
  result-type predicate is gone.
- The result convention (Python bit-vectors vs native SMT Bool/Int) is
  reconciled by the FRONT-END via boundary typecasts (Direction B):
  `emit_string_bool_function` (equal/contains/is_prefix/is_suffix → native
  Bool) and `index_of` (native Int result + Int `from`).
- The Python handler's equal/contains/prefix/suffix branches now serve ONLY
  refined struct operands (the sound SMT2 structural fallback), reached by
  operand type. This is what the bool-query batch needed: NOT deleting the
  branches (which broke refined), but letting String operands route past them.

All gates green: native/cvc5 corpus 50/50; smt2_convt unit 57; [strings] 388;
the refined-struct-under-SMT2 tests (`int-unbounded-box-*`) SUCCESSFUL; full
regression/python green; regression/strings green; jbmc-strings green; sweep
0-reg. This converged equal/contains/is_prefix/is_suffix/index_of onto the
generic encoder for native operands (which the earlier batch could not do), and
removed the front-end discriminator from the op lowering.

### Sort retirement — retried, one blocker remains (also a back-end/front-end coupling)
With the op-lowering discriminator gone, re-attempted the
`smt_string_typet`→`string_typet` rename. Native corpus stays 50/50, but the
`smt2_convt` unit test regresses (14/29) — root cause isolated: `find_symbols`
emits a **Python-specific soundness bound** `(assert (< (str.len s) 2^63))` for
every String-sorted symbol, gated on the sort. It exists because Python reads
`len` as `int2bv(str.len s)` over a bit-vector (Strata uses native Int, needs no
such bound). Renaming applies it to Strata's `ID_string` symbols too → extra
assertions → the exact-match unit test fails. This is the SAME class of smell
(back-end emitting a front-end-specific constraint), just at symbol-declaration
rather than op-lowering. To retire the sort it must move to the FRONT-END (a
per-native-string-symbol `assume len < 2^63`, like `bounded_nondet_string`
already does for some sites) so `find_symbols` stops gating on the sort. That is
a soundness-sensitive move (must cover every native string symbol to preserve
the int2bv-faithfulness the bound guarantees) and is the isolated next step.
Reverted the rename; the op-SORT dispatch (the core fix) is landed.

## Milestone #1 — smt_string sort RETIRED (2026-07-27)

The len bound was moved to the front-end (`8e828c2d1e4`): the len primitives
(`native_or_member_string_length` / `emit_string_int_function`) now emit
`assume(str.len s < 2^63)` at the len read (sound: str.len is deterministic, one
assume constrains it globally; only emitted where the front-end reads len via
int2bv), and `find_symbols` no longer branches on the string sort. Validated
(native 50/50, `len(nondet)>=0` SUCCESSFUL / `==k` cleanly FAILED, suites +
sweep 0-reg).

That was the last back-end sort-gating. With it gone, `smt_string_typet` was
retired (`3b2f7b69d61`): renamed to `string_typet` / `ID_smt_string`→`ID_string`
across front-end + shared back-end; deleted the class, the irep id, and the
duplicate `convert_type`/`convert_constant` branches. **There is now ONE SMT
String sort.** All gates green.

### Milestone #1 outcome
The parallel Python SMT-string backend is substantially retired / unified onto
the shared SMT-LIB-string facility, in landed, individually-validated steps:
- `smt_strcat` id + branch retired; concat → generic `cprover_string_concat`.
- length branch retired; len → generic, Direction B (native Int + typecast).
- equal/contains/is_prefix/is_suffix/index_of converged onto the generic encoder
  via operand-SORT dispatch (the encoder stopped branching on the front-end).
- native-string len soundness bound moved to the front-end.
- `smt_string_typet` sort retired; unified on upstream `string_typet`.

The guiding architectural principle throughout (per review): the back-end
dispatches on the SORT of the operand (String vs refined-struct), never on which
front-end produced it; the front-end reconciles its bit-vector conventions via
boundary typecasts (Direction B) reusing the universal `convert_typecast`.

Residual (documented, gated on operand type / fn_id, NOT front-end): the Python
handler still lowers the genuinely-Python ops (from_int/from_code/to_code/re_ws
and the regex family) by fn_id, and the equal/contains/prefix/suffix structural
fallback serves refined struct operands under an SMT2 backend. These are
semantic dispatches, not front-end coupling, and can be generalised into the
shared encoder later as wider-use work (of_int/replace_all/etc.).

## Milestone #1 — smt_* intrinsic family generalised; milestone closed (2026-07-27)

`e4fe6e1edab`: the six Python-specific `cprover_string_smt_*` ids are gone.
- `smt_strsub` → the existing generic `substring` (flat `(str.substr s o l)`;
  positions via an unsigned-bv hop so the encoder emits plain `bv2nat` — the
  signed conversion nested in `str.substr` positions is a cvc5 perf cliff,
  observed as a hang; front-end clamps positions non-negative).
- `smt_strreplace` → NEW generic `replace_all` → `str.replace_all` (wider-use).
- `smt_from_code`/`smt_to_code` → NEW generic `from_code`/`to_code` (chr/ord),
  Direction-B Int results.
- `smt_from_int` → NEW generic `from_int` + the sign handling COMPOSED in the
  front-end from generic ops.
- `smt_re_ws` → deleted; strip composes Python-whitespace regex membership from
  the generic regex vocabulary (PLR: \x01 kept, unlike Java trim — probed).

Two TYPE-based whole-group fixes surfaced by this work (both latent for any
front-end):
- `find_symbols` no longer declares UFs whose signature involves RegLan (not a
  first-class SMT-LIB sort; cvc5 rejects — the regex intrinsics are lowered
  inline). Latent Strata bug: the unit test never runs a solver.
- goto-symex side-effect const-prop skips value-returning (String-typed)
  applications (no output args; the refined handlers PRECONDITION on them).

**Milestone #1 final state:** ONE SMT String sort; ONE generic
`cprover_string_*`/`cprover_regex_*` vocabulary lowered by the shared encoder
for all String-sorted operands; Direction-B boundary typecasts reconcile
Python's bit-vector conventions; the Python-specific smt2_conv handler retains
ONLY (a) the refined-struct structural fallback (equal/contains/prefix/suffix
under SMT2, dispatched on operand type) and (b) the regex-match decomposition
family (match/search/fullmatch/re_sub/re_group/re_pos — semantic ops with
capture-group/fallback logic, dispatched by fn_id). All gates green
(native 50/50, smt2_convt 57, [strings] 388, python suite, C strings,
jbmc-strings, sweep 0-reg).

## Java front-end benefits from the string/regex unification (2026-07-28)

Follow-on from milestone #1, demonstrating the shared facilities' wider use:

- **Whitespace-set soundness fix (`b7cdbe49f3f`).** CPython's str.strip()/
  isspace() include \x1c-\x1f (verified against CPython); the modelled set
  {09-0d, 20} was a false-proof class. Corrected to two contiguous ranges
  {09-0d, 1c-20} in the refined axiom, the native regex encoding (re.range),
  and the constant folds. Java's Character.isWhitespace agrees on exactly this
  ASCII-range set, so the predicate is shared. Sweep: `string-rstrip-nondet`
  PASS->TOERR is a CORRECTED FALSE PROOF (the ESBMC test's property is
  factually wrong for CPython; ESBMC passes it only by sharing the
  incomplete-set bug).
- **Java 11 String.strip/stripLeading/stripTrailing (`76eaf3053f0`, amended).**
  Thin wrappers over the shared parameterised strip facility
  (cprover_string_strip_func + mode). New jbmc-strings/StringStrip tests incl.
  the strip-vs-trim semantic pin (\x1b kept by strip, removed by trim).
- **String.matches + strings under SMT2 (`2afc9bc2959`).** Three semantic
  enablers: intrinsics emitted whenever refinement OR an SMT2 back-end is
  selected; the symex string-content materialisation extended to any language
  under SMT2-without-refinement (new symex_configt flag); a NEW
  cprover_string_java_matches_func intrinsic gated on
  regex_in_python_java_common_core() -- the conservative dialect guard that
  keeps a Java pattern sound under the Python-dialect translator (rejects
  [a&&b], \p, \Q, possessive quantifiers, etc. -> sound nondet). Constant
  matches now decide exactly on the DEFAULT backend; symbolic subjects are
  regex-constrained under --z3/--cvc5 --no-refine-strings (0.2s for the
  [0-9]+ nonemptiness implication -- beyond the SAT refinement); no false
  proofs (possessive/intersection probes stay FAILED; vacuity ruled out).

Remaining opportunities (not scheduled): Pattern.matches/compile static forms,
Java-dialect regex_char_classest instantiation (UNICODE_CHARACTER_CLASS), and
the native-String-sort representation for Java strings under SMT2 (the object-
model question from the earlier Java-migration assessment still applies).

## Java regex: dialect semantics, Java-only syntax, Pattern/Matcher (2026-07-28)

- **`7c90d3b25b3`** dialect-correct `.` and `\s`: fixed a live PLR false proof
  (Python `\s` missing `\x1c-\x1f` within ASCII) and split the shared classes
  per dialect (Java `.` excludes 5 line terminators; Java's regex `\s` is
  `[ \t\n\x0B\f\r]` -- a THIRD whitespace set, distinct from Python's `\s`
  and from Character.isWhitespace). regex_char_classest gained dot_excluded;
  the concrete byte matcher is dialect-parameterised.
- **`6609b0c647f`** Java-only syntax lowered to the common core, JLS-exactly:
  `\Q...\E`, `\p{POSIX}` classes, `[a&&b]` intersection + nested-class unions
  via a (set, complemented) ASCII algebra (complements stay symbolic so
  non-ASCII behaviour is exact). All cases differentially validated against a
  real JVM; a real emitter bug (ranges over escaped endpoints) was caught by
  the differential probes.
- **`b3741695cbf`** Pattern/Matcher: the model (SUBMODULE commit `9bfe328` on
  branch regex-matcher-model -- local only, needs its own upstream PR) stores
  (pattern, flags, text) and delegates to the intercepted machinery with two
  JLS-faithful staleness flags (region vs find-position). New java_find /
  java_looking_at solver ids (search/anchored-match kinds; trailing-`$`
  rejected for partial kinds -- Java's before-final-terminator rule). Also
  removed the old model's vacuity-inducing "pattern has no metacharacters"
  assume. compile-with-flags != 0 falls back to nondet.

All gates green throughout (jbmc-strings incl. new StringMatches.dialect/
javaSyntax + PatternMatcher tests, [strings] 388, python suites, native
corpus, sweep 0 changes).

## UPDATED PR-series packaging plan (2026-07-28) — post-milestone

Supersedes the "Python PR stack" section above where they conflict. The
milestone work (one String sort, generic encoder vocabulary, dialect-
parameterised regex, JBMC regex/strip features) both shrank the Python-coupled
surface and created NEW independently-valuable material. Proposed series:

### Wave 1 — ready now (branches exist, origin/develop+1 each, build standalone)
1. `python-upstream-01-irep-compare-sharing`
2. `python-upstream-02-ieee-sign-builders`
3. `python-upstream-03-slice-string-intrinsics`
4. `python-upstream-04-simplify-string-guards`

### Wave 2 — new standalone generic PRs (to be cut from the arc's commits;
each is small, Python-independent, and fixes/serves shared infrastructure)
5. **smt2 find_symbols: no UF declarations with RegLan in the signature**
   (latent bug for ANY front-end emitting the regex intrinsics; cvc5 rejects
   RegLan as a UF domain sort). From `e4fe6e1edab`.
6. **goto-symex: skip side-effect const-prop for value-returning (String-
   typed) string applications** (the refined-convention handlers PRECONDITION
   on output arguments). From `e4fe6e1edab`.
7. **smt2: generic string-op lowerings replace_all / from_code / to_code /
   from_int + unit-test coverage** (wider-use SMT-LIB primitives; includes the
   ids). From `e4fe6e1edab` + `186f103797a`.
8. **strings: parameterised strip facility** (Java trim becomes a thin
   wrapper; enables per-dialect whitespace predicates). From `6e4b7310d4c`.
9. **strings: whitespace-set corrections** (strip/isspace ASCII-range set
   incl. \x1c-\x1f; PLR- and Character.isWhitespace-verified). Solver portion
   of `b7cdbe49f3f`. Depends on 8.

### Wave 3 — JBMC feature PRs (upstream-attractive; carry the shared regex
machinery WITHOUT the Python front-end)
10. **JBMC: Java 11 String.strip/stripLeading/stripTrailing** (`2d4cca91f9a`).
    Depends on 8+9 and the strip axiom (mode-parameterised
    cprover_string_strip_func — extract from the Python-coupled commit; it is
    JBMC-valuable independently).
11. **java-models-library PR** (submodule `9bfe328`, branch
    regex-matcher-model): Pattern/Matcher model. MUST land before 12's
    gitlink.
12. **JBMC: regex support** — String.matches + Pattern/Matcher +
    java_find/java_looking_at, carrying the (now dialect-parameterised)
    regex→RegLan translator, the smt2 regex-family lowering, the Java-syntax
    preprocessor, and the strings-under-SMT2 enablers (emission gate + symex
    materialisation flag). From `0bcccff684d`+`598e6e254fa`+`6609b0c647f`+
    `b3741695cbf`. Consider renaming python_regex_to_smt → regex_to_smt in
    this PR (it is a generic dialect-parameterised translator now); the
    Python front-end then arrives as just another dialect client.

### Wave 4 — the Python front-end stack (what remains coupled)
The milestone shrank this: no smt_string sort, no parallel smt2 handler ops,
no Python-specific find_symbols/goto-symex gating (the materialiser's Python
gate remains, documented). Structure as before (backend support → symex hook
→ frontend → tests → docs), re-derived in `upstreaming-linear`.

### Ordering constraints
- 5–9 are independent of each other except 9-after-8; all independent of
  Wave 1.
- 10 needs 8+9; 12 needs 5 (RegLan declarations), 11 (models), and the
  translator; 12 is where the shared regex machinery lands.
- Wave 4 rebases on whatever of Waves 1–3 has landed; every piece it needs
  that lands earlier shrinks it.

## Wave-2 branches PREPARED (2026-07-28)

- `python-upstream-05-smt2-reglan-declarations` (+1): find_symbols RegLan-UF
  fix, with a NEW full-output unit test (the existing get_assert helper
  strips pre-assert output, which is why the bug was never caught; the test
  fails without the fix).
- `python-upstream-06-symex-value-returning-strings` (+1): type-based guard;
  regression/strings green on-branch.
- `python-upstream-07-smt2-generic-string-ops` (+1): replace_all/from_code/
  to_code/from_int ids + map entries + unit coverage (65 assertions).
- `python-upstream-08-strings-parameterised-strip` (+1): trim -> thin wrapper
  over the generic add_axioms_for_strip. Adapted to develop's
  string_constraintt ctors (the ns-threading is a separate Bucket-B
  land-first item). [strings] unit + regression/strings + jbmc-strings green
  on-branch.

RE-PACKAGING NOTE: planned Wave-2 item 9 (whitespace-set corrections) has NO
standalone develop-applicable content -- it only touches the mode-
parameterised strip axiom and Python-side folds, which don't exist on
pristine develop. It folds into the Wave-3 Java-strip PR (where the
whitespace-strip axiom first lands, with the corrected {09-0d,1c-20} set).

## Status update (2026-07-28): Waves 1+2 are in PR

Wave-1 (01-04) and Wave-2 (05-08) branches have all been turned into PRs by
the maintainer. Wave-3 next: (a) the java-models-library Pattern/Matcher PR
(submodule branch regex-matcher-model, commit 9bfe328) MUST go first; (b)
Java 11 strip family (needs 08 + the mode-parameterised whitespace-strip
axiom with the corrected {09-0d,1c-20} set); (c) JBMC regex support (String.
matches / Matcher, the dialect translator -- consider the regex_to_smt
rename), whose main-repo commit bumps the submodule gitlink and so lands
after (a).

## Status (2026-07-28, evening): models-library PR created

The java-models-library Pattern/Matcher PR is up (branch regex-matcher-model
in the user's clone, master+1 `deabf32`; conflicts vs master's newer
cproverIsPlainString refactor resolved toward the delegation semantics;
mvn-built and functionally validated against our jbmc). In flight: Wave 1
(4 PRs), Wave 2 (4 PRs), models-library (1 PR). Wave-3 jbmc branches (Java
strip family; JBMC regex support incl. the submodule-pin bump) are next to
prepare; both stack on Wave-2's 08 / the models PR, so they should be cut
once those land (or as explicitly-stacked draft PRs if earlier visibility
is wanted).
