# TypeScript frontend: symbolic-string SAT scalability (historical)

Date: 2026-05-12 (closed)

## Summary

The symbolic-string KNOWNBUG suite is now empty. The two tests
that historically blew out the SAT encoding —
`string-symbolic-realistic` and `string-trim-symbolic` — were
closed by:

1. Commit `05dbb58806`: provenance-gated `===` routing through
   `cprover_string_equal_func` (closes `string-symbolic-realistic`).
2. Commit `c7fb844a60`: unconditional counter-example addition on
   index-set exhaustion in `string_refinementt::dec_solve`
   (closes `string-trim-symbolic`).

This document stays for historical record and captures the
measurements that motivated the two commits.

## Measured numbers (MiniSat, default settings)

Before commit `05dbb58806`:

| Assertion | Peak vars | Peak clauses | Runtime | Verdict |
|-----------|-----------|--------------|---------|---------|
| `s.trim().length === 5` | 1.1 M | 4.0 M | 2.5 s | ✓ SUCCESS |
| `s.trim() === "hello"` | 16.5 M | 62.5 M | 43 s | ✗ Out of memory |

After:

| Assertion | Peak vars | Peak clauses | Runtime | Verdict |
|-----------|-----------|--------------|---------|---------|
| Single concat `(a + "bar") === "foobar"` | 6.8 M | **900 K** | 4.6 s | ✓ SUCCESS |
| `string-symbolic-realistic` (multi-op chain) | 33 M | 100 M | 137 s | ✓ SUCCESS (needs ~12 GB) |
| `s.trim() === "hello"` | — | — | — | ✗ ERROR (solver index-set issue) |
| `s.trim().length === 5` (`"  hello  "` input) | — | — | — | ✗ ERROR (same) |

The trim-symbolic failure is NOT a SAT-memory issue any more; the
solver's refinement loop reports `"current index set is empty,
this should not happen"` even for the length-only assertion. That
is a CBMC-core solver issue around how `add_axioms_for_trim`
interacts with index-set refinement, not an encoding-size
problem, and sits outside the TypeScript frontend.

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

### Implemented (commit `05dbb58806`)

- **Provenance-gated `===` routing.** When at least one operand of a
  TypeScript string `===` is the struct-exprt output of
  `ts_call_string_returning_function` (identified by the presence of
  `function_application_exprt` nodes in its data field), rewrite the
  equality as a `cprover_string_equal_func` call instead of falling
  through to the per-slot struct compare. The solver emits a single
  compact `length_eq ∧ ∀ i<len: s1[i]=s2[i]` pair rather than 64
  per-slot char_at_func applications plus quadratic Ackermann
  extensionality. The gate on solver-produced operands preserves the
  existing per-slot path for `__CPROVER_assume(s === literal)` shapes
  where `s` is a plain nondet symbol — routing those through
  `equal_func` instead breaks the solver with "current index set is
  empty" because the assume's universal quantifier has no axioms to
  seed the index set from.

### Attempted and reverted

- **Direct `res_arr[i]` indexing** in
  `ts_call_string_returning_function` instead of per-slot
  `char_at_func`. The solver's refinement loop relies on
  `char_at_func` calls to populate its index set for universal-
  quantifier instantiation; bypassing them broke content precision
  across the board with the same "current index set is empty"
  error that plagues `string-trim-symbolic`. See commit message of
  `05dbb58806` for details.

### Not yet implemented

### Shrink `TYPESCRIPT_MAX_STRING_LENGTH` where it is provably safe

The 64 constant comes from `typescript_types.h`. The SAT cost is
quadratic in this value via Ackermann on the input-side of
`ts_string_to_refined`. Lowering it to 32 would halve the per-slot
count and quarter the Ackermann pair count on the assume path.
Trade-off: programs with longer literal strings would truncate
silently.

A cleaner variant: keep the inline array at 64 but, when comparing
two refined-strings, emit only the first `max(len_a, len_b)`
per-slot disjuncts (currently we emit all 64 unconditionally). The
out-of-range positions are logically zero on both sides, so they
never contribute to the disequality; elliding them saves both the
comparison and the `char_at` calls they induce. This would help
the assume-side `s === literal` shape which the provenance gate
above deliberately keeps on the per-slot path.

### Short-circuit `char_at_func` at constant indices known to
exceed the refined length

Each per-slot readout is guarded by `i < result_len` but the
guarded branch still generates a `char_at_func` expression that
boolbv records. At constant `i` ≥ an upper bound on `result_len`
(derivable from the input-string lengths in concat / repeat /
trim), we can fold the readout to the 0 branch without generating
the call. For trim specifically, `result_len ≤ input_len`, which
at conversion time is a known upper bound for literal inputs — so
many of the 64 per-slot `char_at_func` calls are provably dead.
Less impactful now that `===` on solver-produced strings bypasses
the per-slot readout entirely via the provenance gate above.

### Collapse the Ackermann pairs structurally

Tracked in a separate branch (not in this commit history).

## Remaining KNOWNBUG

None in the symbolic-string suite.

The `string-trim-symbolic` failure (previously documented here) was
traced to the refinement loop in `string_refinementt::dec_solve`:
when trim's axioms 6/7 have fully enumerated their bounds and the
SAT model still violates a universal at some witness index,
`update_index_set(current_constraints)` returns no new indices and
the old code bailed with `"dec_solve: current index set is empty,
this should not happen"`. The fix (commit `c7fb844a60`) is to
unconditionally add `check_axioms`'s counter-examples as ground-
level lemmas in that path — the previous guard on
`axioms.not_contains.empty()` was arbitrarily strict. Counter-
examples are valid progress for any universal axiom, not just
not_contains. With the fix, the loop now either converges to
SAT/UNSAT or exhausts `loop_bound_`, never the stuck state.
