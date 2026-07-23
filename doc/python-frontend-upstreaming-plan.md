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
