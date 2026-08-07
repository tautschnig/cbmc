# Closed-form comprehensions (no-unwind encoding)

Status: **P1–P3 landed** (map subset; quantified genexp aggregates;
quantified membership, list equality, list.index and min/max —
2026-08-06). Open: enumerate/zip closed-form producers (loud false
alarm today), filter axiomatization (P4, only if demanded).
Owner doc for the comprehension arm of
[python-frontend-unbounded-containers-plan.md](python-frontend-unbounded-containers-plan.md).

## 1. Problem

Comprehensions lower to GOTO loops (`emit_listcomp_loop`: scan the
iterable, filter, store, count). Under `--python-smt-containers`
container lengths are SYMBOLIC, so these loops are unbounded: any
`--unwind K` yields an unwinding-assertion failure (loud) or, with
`--no-unwinding-assertions`, silent truncation at K elements — the
boundedness the containers flag exists to remove re-enters through
every comprehension. Demonstrated: `[x * 2 for x in fetch()]` fails
`.unwind.0` at any bound before the fix.

## 2. Backend support matrix (measured, this tree)

| construct | z3 (`--z3`) | cvc5 (`--cvc5`) | generic `--smt2` | boolbv/SAT |
|---|---|---|---|---|
| `array_comprehension_exprt`, infinite size | `(lambda …)` — proofs, counterexamples, models through the lambda all work (4.8.12 and 4.13.4) | ∀-axiom form; **proof direction works**, counterexample direction may return `unknown` (loud ERROR, never a false proof) | ∀-axiom, same as cvc5; z3 as the actual solver discharges both directions | `convert_array_comprehension` exists but requires a CONSTANT size — not applicable to infinite arrays (containers flag implies smt2 anyway) |
| `index(comprehension, i)` at symex | folded by the simplifier via substitution (`simplify_expr_array.cpp`) — concrete tests keep folding, no solver load added | same | same | same |
| symex renaming of the binding | bound var must be a REGISTERED symbol (L0 lookup); binder+body rename consistently — same route as C-frontend quantifiers | | | |
| composition (map of map), store-over-comprehension | proven at both the SMT level (hand probes) and end-to-end | | | |

Two enabling fixes landed with the spike:
- smt2 ∀-axiom emission crashed on `convert_expr(infinity)` for
  infinite-size comprehension types; it now quantifies over the whole
  index domain (= the lambda semantics) when the size is
  `ID_infinity`.
- the frontend registers the bound variable in the symbol table
  (symex L0 requirement).

## 3. Subset taxonomy

**Tier 1 — MAP (landed).** `[elt(x) for x in xs]`, single generator,
no filters, `elt` PURE. Closed form:
`{length: xs.length, data: array_comprehension j. elt[x := xs.data[j]]}`.
Exact at any length; composes; concrete cases fold at symex.
Purity gate (conservative, syntactic): the element conversion emitted
NO pending statements — rejects may-raise operations (division,
subscript with checks), side-effecting or user calls, anything that
needs a per-iteration guard. Ineligible comprehensions fall through
to the loop lowering unchanged (loud under unwinding assertions).

**Tier 2 — quantified AGGREGATES (LANDED).** `all(p(x) for x in xs)`
→ `∀ j ∈ [0, len): p(data[j])`; `any(...)` → ∃. Assertion-position
`all`/`any` are the natural first target (symex already supports
∀/∃ in assert/assume with the smt2 backend; `--python-assume-inputs`
precedent). Value-position needs the boolean materialized — still
closed-form. `in` over a comprehension result: already scan-based;
compose the map body into the scan needle instead of materializing.

**Tier 3 — FILTER (no closed form; do not attempt).**
`[x for x in xs if p(x)]` is a COMPACTION: `result[j]` is the j-th
element satisfying p — inherently sequential (prefix counting), not
expressible as an index-wise lambda. Options, in preference order:
(a) keep the loop (bounded producers stay exact; symbolic-length
falls to the unwinding assertion — loud); (b) axiomatize the length
only (`0 ≤ len(result) ≤ len(xs)`) plus a ∀ "every element satisfies
p ∧ came from xs" — a sound OVER-approximation of reads that loses
order/multiplicity; behind a sub-flag if ever. len-only properties
(`len([... if p]) == count`) additionally need a sum — out of scope.

**Tier 4 — DICT/SET comprehensions (blocked on semantics).**
`{k(x): v(x) for x in xs}`: duplicate keys collapse (last wins), so
`length` is the number of DISTINCT keys and the value at k is the
LAST x with k(x)=k — both inherently sequential. The injective
special case (`enumerate`-style keys) is closed-form but detecting
injectivity soundly is the hard part. Set comprehensions have the
same distinctness problem. Keep loops.

**Generator expressions** consumed by an aggregate = Tier 2. A
genexp materialized into a list = Tier 1 if map-only.

## 4. Soundness constraints

- **Purity is the load-bearing gate**: an element expression that can
  raise must NOT be hoisted into a term (the loop emits its guards
  per-iteration; a term evaluates them nowhere). The empty-
  pending-checks test is exactly "the converter needed no statement
  context", which is the honest syntactic under-approximation of
  purity. Anything rejected keeps today's semantics.
- **Order**: a map is index-wise; PLR order is preserved by
  construction. (Filters/dicts are where order costs — excluded.)
- **Exceptions**: the comprehension site itself cannot raise in the
  map subset (subscripting data[j] on the infinite array has no
  bounds property; length is copied, not scanned — no
  python-model-bound guard needed).
- **cvc5 incompleteness** (∀+arrays, counterexample direction) is
  LOUD (`unknown` → ERROR verdict), never silent.

### 3.1 The architectural seam (landed with Tier 2/3)

Every member of the family is the same shape: a BOUNDED SCAN over an
iteration range that a quantifier lifts exactly. The shared seam
(python_container_ops.cpp):

- `make_iteration_view(iterable)` -> {length, data, elem(j)} for a
  (possibly boxed) list under the flag;
- `fresh_bound_index` (symbol-table-registered bound vars, the symex
  L0 requirement);
- `forall_in_range` / `exists_in_range` (PLR-shaped: vacuous True
  for `all(())`, vacuous False for `any(())` / `x in []`);
- `quantifier_safe_term(e)`: the SOUNDNESS gate. Two conditions,
  applied at every lift site: (1) the trial conversion emitted no
  auxiliary statements (the empirical purity gate: no may-raise
  operations, no side-effecting or user calls -- per PLR 6.10.1 an
  Any-element ordering can raise TypeError and short-circuiting
  makes that ORDER-OBSERVABLE, so Any-element genexps correctly keep
  the bounded lowering); (2) no refined-string solver applications
  under the binder (the string-refinement solver is not
  quantifier-aware; a bound index inside cprover_string_* risks
  wrong axiom instantiation -- observed as a solver hang. Native
  SMT-strings terms quantify soundly).

Consumers wired through the seam: genexp `all`/`any` (assertion and
value position, filters as implication/conjunction), `x in xs` /
`not in` (the per-element match extracted into ONE builder shared by
the bounded scan and the exists-form -- the P0 choke-point
discipline), and list `==`/`!=` (len-equal AND forall elem-eq). The
lifted forms need NO scan-bound guard: python-model-bound properties
disappear exactly where the op became exact.

`index` and `min`/`max` are LANDED via the witness pattern
(`mint_witness_symbol` + guarded assume; the guard is the
feasibility condition -- found / non-empty -- so the PLR
raise-instead-of-value cases leave the witness unconstrained and
the assume never prunes feasible paths). index carries the PLR 6.3
first-occurrence minimality forall and a CATCHABLE ValueError;
min/max are INT-ONLY (an IEEE NaN element falsifies the
forall-bound while CPython ignores/propagates NaN by position --
floats keep the bounded reduction) and their empty-sequence
ValueError became catchable per PLR 8.4 (it was a bare add_check
property that try/except could not intercept -- both configurations
fixed). `in`-over-comprehension fusion needed NO code:
exists-over-lambda composes (probe-validated).

A z3 model-extraction wall was fixed in smt2_conv on the way: the
macro finder inlines `(assert (= b <quantified>))` definitions and
`get-value` then returns unevaluated quantified terms for every
dependent symbol (VERIFICATION ERROR in the falsification direction
of anything in a witness assume's cone). Quantified Boolean
definitions are now emitted as implication pairs -- semantically
identical, not macro-shaped, constants under get-value.

Still not lifted: `sum`/`count` (genuinely sequential),
refined-strings content matches (gate above), dict/set comprehension
(distinctness), enumerate/zip producer
comprehensions are LANDED as MULTI-SOURCE MAPS: a tuple-unpacking
comprehension over enumerate()/zip() never materializes tuples --
each unpacked name substitutes its index-wise source expression
(enumerate start offset rides along; zip length is min per PLR 5.8).
Genexp all()/any() over enumerate/zip remains on the bounded path
(loud fail-closed exceptions, probe-verified -- never silent-wrong);
tuple-ELEMENT comprehensions ([t for t in enumerate(xs)]) keep the
loop lowering (real tuple lists).

## 5. Phasing

- **P1 (landed)**: map subset behind `--python-smt-containers`;
  smt2 infinity-axiom fix; bound-var registration; tests
  (`smt-containers-comp-closedform{,-fail}`: symbolic length,
  element property, composition, concrete folding, symbolic range,
  boxed-element identity map; twin runs WITH unwinding assertions).
- **P2**: assertion-position `all`/`any` genexp aggregates → ∀/∃
  (quantifier emission in the python frontend; gate on the same
  purity test; validate counterexample direction per solver).
- **P3**: value-position aggregates; `in`-over-comprehension fusion;
  map over `enumerate`/`zip` (index-wise, still closed-form).
- **P4 (only if demanded)**: filter length-bounds axiomatization
  behind a sub-flag; dict-comprehension injective special case.

## 6. Spike evidence (2026-08-05)

- SMT-level: lambda and ∀-axiom forms of `ys = map(2·, xs)` prove
  `ys[i] = 2·xs[i]` under symbolic `len` on z3 4.8/4.13 and cvc5
  1.2.1 (incl. sat-direction model through the lambda on z3; store-
  over-lambda; lambda-of-lambda composition).
- End-to-end (`--unwind 6`, unwinding assertions ON): symbolic-length
  len/element/composition properties prove on all three backend
  configs; wrong-value twin FAILS on z3/generic, ERRORs (unknown) on
  cvc5; concrete comprehensions still fold; `range(n)` with symbolic
  n proves; identity map over boxed nested-container elements proves.
- Gates: full python suite green; comprehension+containers
  differential 67/70 (three pre-known diffs, all documented);
  C-frontend Quantifiers-* spot suite green.


## 7. Perf-study intake (2026-08-07, ~/cbmc-perf-study)

The study ran the code-action corpus under containers+native-strings
and diagnosed the wall as SYMEX NON-TERMINATION (comprehension loops
with symbolic trip counts), not solver cost. Re-baselined against
this tree and consumed:

**Fixed here** (commit "consume the perf-study findings"): the
silent symex hang (ineligible comprehensions now bounded+loud under
the flag), stub-container negative lengths (t4_neglen), the
TypedDict-stub ill-typed key store (d1 crash), typed/boxed TypedDict
stub fields, and the study's 5b representative+lift scheme for
may-raise comprehension bodies (validated by the study against this
backend; implemented as a pre-pass to the Tier-1 map: checks run
once at a nondet in-range index, values keep the exact
array_comprehension).

**Already fixed before intake**: its q9 two-quantified-assume
model-parse bug (the implication-pair emission); pure-body
comprehension hangs (Tier-1 map).

**Adopted as design constraints, not yet implemented:**
- smt2_conv emits NO :pattern annotations on quantifiers; the
  frontend's quantified assumes have natural triggers
  (select(data, j)) and nesting multiplies quantifier count.
  Cheap, targeted, worth doing when depth > 1 shows up.
- Dict KEY LOOKUP under the flag is still the 16-slot fail-closed
  scan (storage unbounded, lookup bounded); a quantified presence
  predicate is the same lift as the landed membership one.
- TypedDict NotRequired presence obligations are parsed but not
  enforced on subscript reads (study t5/t6: identical property
  lists); the required-key data currently drives call-site kwarg
  checks only.
- SET slot order is HASH-determined, not insertion order
  (list({3,1,2}) == [1,2,3]) -- any future set closed-form gets
  dedup+minimality+completeness and NO order axiom; dicts keep
  monotonicity (3.7+ guarantee) and the value witness needs
  MAXIMALITY for last-writer-wins.
- Vacuity discipline for quantified encodings: infinite-array
  GLOBALS zero-initialize and can contradict assumed foralls
  (everything proves, incl. assert(0)); regression tests for
  quantified encodings should carry an assert(0) vacuity probe.
- Benchmark reporting: the runtime distribution under the flag was
  BIMODAL (finish fast or never); report survival/censoring rates
  split by hang phase alongside percentiles over finished runs.

**Remaining loud residuals** (pinned):
- stub-typeddict-value-len-knownbug: len() of a container value
  read from a stub TypedDict -- the boxed value's deref loses the
  heap object through the infinite values array (the P3a
  deref-precision class).
- The study's d1/d4 element-SHAPE layer: List[App] elements are not
  yet constrained App-shaped, so the representative's KeyError
  obligation is honestly unprovable (loud FAILED, terminates
  instantly; was an infinite hang).
- A corpus-level to_array_type crash near `sql[:300]`
  (study 6; unminimized).


## 8. Fresh-look scorecard (2026-08-07 pm, full 26-repro corpus)

All 26 study repros, `--python-smt-containers --python-smt-strings
--z3`, 90s/8GiB each. NO silent symex divergence remains anywhere in
the corpus (the last one -- plain for-loops over symbolic lengths,
ex3/ex4 -- fixed this round together with the pv-len representation
invariant).

CORRECT verdicts (17/26): d2 d3 d5 k3 k4 t2 t3 t4 t6 u1 u2 pass;
t1 t5 fail on their intended KeyError obligations; k1's/k2's fails
are honest (see below); ex5's fail is honest (real
ZeroDivisionError on an unconstrained divisor field).

REMAINING, by class:
- Boxed-source residual (d1 d4 repro.py, loud fail <=8s): the
  comprehension source is a boxed List[TD] behind a dict value;
  elements are pv-boxed by design -- the per-instance deref
  precision work (P3a class).
- Dedup semantics (k1 k2 k5, loud fail, CONCRETE inputs):
  dict/set-comprehension clash rules not modeled (dedup /
  first-key-last-value / first-occurrence-object). The study's 5.3
  taxonomy (dedup + minimality + completeness + NO set-order
  axiom, dict value MAXIMALITY) is the design; nothing landed yet.
- ex2/ex7 (nested 2-generator, non-injective dictcomp): fall to the
  bounded fallback; result-shape assertions unproven (loud, fast).
- Solver/symex cost ceilings (ex1 ex3 ex4 ex6, TIMEOUT but
  TERMINATING): filtered/set comprehensions in-solver; ex3/ex4
  bounded for-loops with heavy bodies grow SSA per iteration
  (with-chain drag). The for-loop analogue of representative+lift
  (a check-only loop body is a forall obligation) would collapse
  ex4's second loop exactly like the comprehension case; :pattern
  triggers remain the solver-side lever.


## 9. Scorecard update (2026-08-07 evening)

k1/k2 flip to SUCCESSFUL (clash semantics: dictcomp through
build_dict_value, setcomp bitmap dedup, Name-iterable literal
resolution). 19/26 now behave correctly (was 17). ex4's check loop
takes the new for-loop representative lift (symex milliseconds; its
residual wall is the SOLVER-side provenance entailment). Remaining:
the boxed-source class (d1/d4/repro.py -> structure-of-arrays
design, pinned as smt-containers-boxed-source-knownbug), k5 (user
__eq__ dispatch in dedup), ex2/ex7 result-shape assertions on the
bounded fallback, ex1/ex5/ex6 in-solver ceilings (:pattern item).
