# TypeScript frontend: SAT-scalability profile of symbolic-string KNOWNBUGs

Date: 2026-05-12

## Summary

Two tests remain `KNOWNBUG` in the symbolic-string suite:
`string-symbolic-realistic` and `string-trim-symbolic`. Both hit SAT
memory limits on the same underlying pattern: **comparing a
solver-produced refined string for full content-equality with a
literal** (`s.trim() === "hello"`, `result === "foo: bar"` after a
chain of solver operations).

This document identifies the dominant clause source and lists
concrete ways to reduce the encoding size.

## Measured numbers (MiniSat, default settings)

Baseline and the 15× jump:

| Assertion | Peak vars | Peak clauses | Runtime | Verdict |
|-----------|-----------|--------------|---------|---------|
| `s.trim().length === 5` | 1.1 M | 4.0 M | 2.5 s | ✓ SUCCESS |
| `s.trim() === "hello"` | 16.5 M | 62.5 M | 43 s | ✗ Out of memory |

(`s` is `nondet_string()` assumed `=== "  hello  "`.)

## Root cause: per-slot struct compare on solver-produced strings

Our `===` for strings compiles to a native struct compare
(`equal_exprt`) over the `{length, char[64] data}` layout. That
expands — in the assertion's negation — into `length_differs ∨ ∃
i<64: data[i] differs`, and boolbv unrolls the `∃` into 64 parallel
per-slot disequalities.

For a **solver-produced** receiver (output of
`ts_call_string_returning_function`), `data[i]` is an if-expression:

    data[i] = if_exprt{ i < result_len,
                       cprover_string_char_at_func(refined_result, i),
                       0 }

so each of the 64 positions generates a distinct
`cprover_string_char_at_func(refined_result, constant_i)`
application.

## Multiplicative steps to the 62 M clauses

1. **64 function_applications per solver-produced string operand.**
   Per-slot readout in `ts_call_string_returning_function` produces
   64 `cprover_string_char_at_func(...)` calls, each at a distinct
   constant index.

2. **Ackermann extensionality in boolbv.**
   `boolbvt::convert_function_application` records every
   `char_at_func` call into
   `functionst::function_map`. At `finish_eager_conversion` the
   base class emits quadratic Ackermann: for every pair `(f(a1,b1),
   f(a2,b2))` with the same function symbol, it asserts
   `(a1==a2 ∧ b1==b2) ⇒ result1==result2`. 64 applications per
   refined string ⇒ 64·63/2 ≈ 2 000 pairs; two operands (trim
   input and trim output) in one equation ⇒ ≈ 4 000 implications.
   Each implication bit-blasts to hundreds of clauses after
   comparison encoding on the arguments.

3. **Trim's universal quantifier instantiation.**
   `add_axioms_for_trim` in the string-constraint generator adds
   two universal axioms plus an existential one on the result
   length. The refinement loop instantiates the universal bodies
   at every concrete index the SAT solver chooses during model
   recovery; for a 64-wide problem that cascades into ~100 per-
   iteration lemmas, each fully bit-blasted.

4. **Per-slot compare on the equation.**
   The 64 disjuncts from the struct-compare each carry the full
   per-slot `char_at_func` application tree, so the
   Ackermann-extensionality terms are duplicated across the
   comparison positions.

Combined multiplier: 64 (positions) × 4 000 (Ackermann pairs) ×
≈ 500 (clauses per implication) ≈ 128 M raw terms, which MiniSat
simplifies down to the ~62 M observed peak.

## Where to reduce clauses, in rough order of impact

### 1. Shrink `TYPESCRIPT_MAX_STRING_LENGTH` where it is provably safe

The 64 constant comes from `typescript_types.h`. The SAT cost is
quadratic in this value via Ackermann (step 2). Lowering it to 32
would halve the per-slot count and quarter the Ackermann pair
count. Trade-off: programs with longer literal strings would
truncate silently.

A cleaner variant: keep the inline array at 64 but, when comparing
two refined-strings, emit only the first `max(len_a, len_b)`
per-slot disjuncts (currently we emit all 64 unconditionally). The
out-of-range positions are logically zero on both sides, so they
never contribute to the disequality; elliding them saves both the
comparison and the `char_at` calls they induce.

### 2. Route `===` through `cprover_string_equal_func` when either
operand is solver-produced

The `string_constraint_generator_testing` entry for
`cprover_string_equal_func` emits a compact
`length_eq ∧ ∀ i<len: s1[i]=s2[i]` pair. On our numbers that is
O(length) SAT variables vs O(TYPESCRIPT_MAX_STRING_LENGTH²)
extensionality. Earlier experiments routing all symbolic `===`
through the solver broke the assume side with "current index set
is empty" in the refinement loop. A narrower predicate that only
routes when at least one side originates from a refined-string
function call (and not from `__CPROVER_assume(s === literal)`
where `literal` is a compile-time struct) would likely avoid the
index-set corner case. Requires tracking solver-provenance on
string expressions.

### 3. Short-circuit `char_at_func` at constant indices known to
exceed the refined length

Each per-slot readout is guarded by `i < result_len` but the
guarded branch still generates a `char_at_func` expression that
boolbv records. At constant `i` ≥ an upper bound on `result_len`
(derivable from the input-string lengths in concat / repeat /
trim), we can fold the readout to the 0 branch without generating
the call. For trim specifically, `result_len ≤ input_len`, which
at conversion time is a known upper bound for literal inputs — so
many of the 64 per-slot `char_at_func` calls are provably dead.

### 4. Collapse the Ackermann pairs structurally

`char_at_func(s, i)` applications with the same `s` and distinct
constant `i` are known-distinct: the extensionality implication
`(s==s ∧ i==j) ⇒ result_i==result_j` is only interesting when
`i==j` is satisfiable, which it never is for distinct constants.
A targeted override of
`functionst::add_function_constraints` that skips the pair when
one pair of arguments is distinct-constant would eliminate most of
the 4 000 pairs per trim assertion.

## Which of these to do first

- #3 is local to `ts_call_string_returning_function` and preserves
  semantics exactly; likely the cheapest win.
- #4 is in `functions.cpp` (a CBMC-core file) and would benefit
  every refined-string user, not just TypeScript.
- #2 needs a provenance flag on exprts (non-trivial but the
  biggest precision win in the long run).
- #1 is a user-visible behaviour change and is a last resort.

None of these are done yet; both KNOWNBUGs remain marked as such
in `regression/typescript/`.
