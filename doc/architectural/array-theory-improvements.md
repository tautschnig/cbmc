# Array Theory Improvements

## Overview

This document tracks the work to improve the array-theory encoding in CBMC's
propositional back-end. The goal is to achieve correctness first, then
performance, informed by Christ and Hoenicke's "Weakly Equivalent Arrays"
paper (arXiv:1405.6939).

Branch: `array-theory-improvements` off `origin/develop`.

## Background

### Current Architecture

The array theory lives in `src/solvers/flattening/arrays.cpp`. It uses a
**union-find** (`union_find<exprt>`) to group arrays into equivalence classes,
then generates three kinds of constraints:

1. **Element-wise constraints** for `with`, `if`, `array_of`, `array_constant`,
   `comprehension`, `typecast`, `let` — e.g., for `x = (y with [j:=v])`:
   - `x[j] = v` (direct write)
   - `j ≠ I → x[I] = y[I]` for each index `I` in the index set

2. **Equality constraints** — for each array equality `a = b` and each index
   `i`: `(a = b) → a[i] = b[i]`

3. **Ackermann constraints** — for each pair of indices `i₁, i₂` in the same
   equivalence class: `i₁ = i₂ → a[i₁] = a[i₂]`. This is quadratic in the
   number of indices.

The `--arrays-uf-always` flag forces all arrays through this theory (even
small ones that could be flattened to bitvectors). The `--arrays-uf-never`
flag disables it entirely. The default (`U_AUTO`) uses the theory for arrays
whose bitvector width exceeds `MAX_FLATTENED_ARRAY_SIZE` (1000 bits).

### The Paper (Christ & Hoenicke, 2014)

Key concepts:

- **Weak equivalence:** Two arrays are weakly equivalent if connected by a
  chain of store operations. They can differ only at finitely many indices.
- **Weak equivalence modulo i (`a ≈ᵢ b`):** Connected by a path where no
  store index equals `i`.
- **read-over-weakeq (Lemma 1):** If `a ≈ᵢ b` and `i ~ j`, then
  `a[i] = b[j]`.
- **weakeq-ext (Lemma 2):** If `a` and `b` are connected by path `P`, and
  for all `i ∈ Stores(P)` we have `a ~ᵢ b`, then `a = b`.

The paper's data structure uses a forest with primary and secondary edges,
achieving linear space. The key advantage over the current architecture is
that it replaces the quadratic Ackermann constraints with targeted path-based
lemmas.

### Prior Attempts

The `tautschnig/array-speedup` branch (2017) attempted a full WEG
implementation using `grapht<weg_nodet>` and DFS path enumeration, but was
abandoned — most constraint generation is inside `#if 0` blocks, `update`
is `UNIMPLEMENTED`, extensionality is commented out.

The `tautschnig/array-fixes` branch takes a conservative approach: keeps the
union-find but adds targeted bug fixes and the Ackermann skip optimisation.
17 commits on top of develop.

## Phased Plan

### Phase 0: Infrastructure ✅
- Create branch `array-theory-improvements` off `origin/develop`
- Fetch SMT-COMP QF_AX benchmarks (551 benchmarks from SMT-LIB 2025 release
  on Zenodo, families: storecomm, storeinv, swap, cvc)
- Add `declare-sort` support to smt2_solver parser (maps uninterpreted sorts
  to 32-bit bitvectors)
- Add 12 representative benchmarks as regression tests (6 CORE, 6 KNOWNBUG)
- Establish baseline: 35.0% correct rate (2s timeout)

### Phase 1: Cherry-pick bug fixes ✅
- Accept member expressions with arbitrary struct operands in `collect_arrays`
  and `add_array_constraints` (from `07ca35095d`)
- Simplify after lowering byte operators in array equalities (from `5eda44dc13`)
- Handle array typecast with different element sizes — skip union-find merge
  and element-wise constraints when element types differ (from `04c45d53fa`)
- Add with-constraints for SSA-renamed indices (from `ed378e6e27`)
- QF_AX impact: none (these fix CBMC-specific issues)

### Phase 2: Ackermann skip optimisation ✅
- Skip Ackermann constraints for derived arrays: `with`, `if`, `array_of`,
  `array_constant`, `comprehension`, `typecast`, `let` (from `01609da27f`)
- This is the core "weak equivalence" insight from the paper
- QF_AX impact: 18 fewer timeouts, 9 more correct (35.0% → 36.6%)

### Phase 3: Investigate and fix incompleteness ✅
- **Root cause identified:** missing extensionality axiom
- All 192+ wrong answers require proving array equality, which needs
  `(∀i. a[i] = b[i]) → a = b`
- The theory only had the forward direction: `a = b → a[i] = b[i]`
- **Fix implemented:** Skolemized extensionality via diff indices
- **Optimizations explored:** 7 different approaches (see below)
- **Conditional extensionality:** skip diff indices for asserted-true
  equalities — zero overhead for typical CBMC usage
- **Full CBMC regression suite:** 1173/1173 pass, zero regressions
- **Lazy extensionality for --refine-arrays:** implemented but slower
  than eager for QF_AX benchmarks; useful for CBMC where most formulas
  don't need extensionality

### Phase 4: Map abstraction layer (future)
- Introduce a `map_theoryt` class separating map concepts from array concepts
- Move index tracking and Ackermann generation into it

### Phase 5: Full weak equivalence graph (future)
- Replace union-find with proper WEG data structure
- Implement read-over-weakeq lemma generation (Lemma 1)
- Implement weakeq-ext for extensionality (Lemma 2)

## QF_AX Benchmark Results

551 benchmarks from SMT-LIB 2025 (storecomm: 210, storeinv: 38, swap: 302,
cvc: 1). All wrong answers are `sat` when `unsat` expected (incompleteness).
Zero wrong `unsat` (theory is sound).

### 30-second timeout, 8 parallel jobs (definitive results)

| Stage | Correct | Wrong | Timeout | Rate | CPU time |
|-------|---------|-------|---------|------|----------|
| 1. Baseline (declare-sort only) | 211 | 202 | 138 | 38.2% | 4688s |
| 2. +Bug fixes + Ackermann skip | 247 | 236 | 68 | 44.8% | 3245s |
| 3. +Extensionality | 378 | 72 | 101 | 68.6% | 4608s |
| 4. +Inline let bindings | **449** | **0** | 102 | **81.4%** | 4863s |

Key observations:
- Stage 2 halves timeouts (138→68) and reduces CPU time 31% via Ackermann skip
- Stage 3 fixes 166 wrong answers via extensionality; timeouts rise slightly
  (68→101) due to diff index overhead
- Stage 4 fixes all remaining 72 wrong answers via let inlining; zero perf cost
- **Zero wrong answers** in the final stage — theory is complete for all
  benchmarks that finish within the timeout
- 102 remaining timeouts are purely performance (storecomm family dominates)

### 2-second timeout (quick iteration results)

| Stage | Correct | Wrong | Timeout | Rate |
|-------|---------|-------|---------|------|
| 1. Baseline | 193 | 184 | 174 | 35.0% |
| 2. +Bug fixes + Ackermann skip | 202 | 193 | 156 | 36.6% |
| 3. +Extensionality | 258 | 72 | 221 | 46.8% |
| 4. +Inline let bindings | 304 | 0 | 247 | 55.1% |

### Breakdown by benchmark family (with all fixes, 2s timeout)

- **storecomm** (210): mostly timeout (deep store chains → expensive)
- **storeinv** (38): all correct (extensionality fixes these)
- **swap** (302): all correct that finish; some timeout on larger instances
- **cvc** (1): correct (read5.smt2 — complex store chain equality)

### Extensionality Optimization Attempts

| # | Approach | Correct | Wrong | Timeout | Verdict |
|---|----------|---------|-------|---------|---------|
| 0 | No extensionality | 202 | 193 | 156 | Fast but very incomplete |
| 1 | Diff per equality | 137 | 41 | 373 | Most complete, slowest |
| 2 | Diff per class | 258 | 72 | 221 | Best balance (chosen) |
| 3 | Skip all diff Ackermann | ~150 | ~48 | ~350 | **UNSOUND** — breaks storeinv |
| 4 | Targeted element-wise | 110 | 27 | 414 | Worse: indirect index pollution |
| 5 | Index-set extensionality | +1 | 0 | 0 | Marginal help for propagation |
| 6 | Skip diff-vs-diff Ackermann | +1 | 0 | 0 | Small improvement (chosen) |
| 7 | Conditional extensionality | — | — | — | Skip for asserted-true eq (chosen) |

**Conditional extensionality (#7):** Track which array equality literals are
asserted true (via `set_to(expr, true)` or `record_array_let_binding`). Skip
extensionality for those equalities since the forward direction suffices.
This is critical for CBMC usage where SSA equalities dominate — the
Array_UF23 test has Ackermann count 60 (same as without extensionality)
because all its equalities are asserted true. Full CBMC regression suite
passes with zero regressions (1173 tests).

**Duplicate block bug:** An earlier version accidentally had two extensionality
blocks (one per-class, one per-equality). Removing the duplicate improved
QF_AX results from 25.9% to 46.8% correct rate.

**Why #3 is unsound:** The diff index needs Ackermann constraints against
store indices to propagate through the store chain. For `storeinv`:
`store(a1, i1, a2[i1]) = store(a2, i1, a1[i1])` — the diff index `d` for
`a1 = a2` needs `d = i1 → a1[d] = a1[i1]` to connect the diff value to
the store at `i1`.

**Why #4 is worse:** Calling `add_array_constraints(diff_set, ...)` for each
array in the equivalence class triggers `convert()` calls that add the diff
index to the global index set indirectly through `record_array_index`.

## Investigation: Wrong Answers

### Root Cause

**Missing extensionality axiom.** The array theory encoded:
- Forward: `(a = b) → a[i] = b[i]` for each index `i` ✓
- Reverse: `(∀i. a[i] = b[i]) → (a = b)` ✗

Without the reverse direction, the SAT solver can set `a ≠ b` even when all
elements are equal, because nothing forces the equality literal to be true.

### Minimal Reproducer

```smt2
(declare-sort Index 0)
(declare-sort Element 0)
(declare-fun a1 () (Array Index Element))
(declare-fun a2 () (Array Index Element))
(declare-fun i1 () Index)
(assert (= (store a1 i1 (select a2 i1)) (store a2 i1 (select a1 i1))))
(assert (not (= a1 a2)))
(check-sat)
```

Expected: `unsat`. Without extensionality: `sat` (wrong).

### Proof Sketch (why extensionality is needed)

Let `L = store(a1, i1, a2[i1])`, `R = store(a2, i1, a1[i1])`.
1. `L = R` is asserted → `L[i1] = R[i1]` → `a2[i1] = a1[i1]`
2. For any `j ≠ i1`: `L[j] = a1[j]`, `R[j] = a2[j]` → `a1[j] = a2[j]`
3. Combined: `∀j. a1[j] = a2[j]` → by extensionality: `a1 = a2`
4. But `a1 ≠ a2` is asserted → contradiction → `unsat`

Step 3 requires extensionality. Without it, the solver can satisfy the
formula by choosing `a1 ≠ a2` while having all elements equal.

### Remaining 72 Wrong Answers → FIXED

Root cause: the SMT2 parser's `let_expression()` created `let_exprt` nodes.
The solver's `convert_let()` created fresh symbols with temporary bitvector
mappings that were erased after the let body was converted. The array theory
generates constraints later (during `finish_eager_conversion`) and could no
longer resolve the fresh symbols.

Element-typed let bindings (e.g., `(let ((v (select a i))) ...)`) created
an abstraction barrier: the array theory's with-constraints used the fresh
symbol as an opaque value, disconnected from the array select it represented.

Fix: substitute let bindings inline in the SMT2 parser using `replace_symbolt`
instead of creating `let_exprt`. This eliminates the abstraction barrier.

Result: all 72 previously wrong answers are now correct. **Zero wrong answers
remain** — the array theory is complete for all QF_AX benchmarks that finish
within the timeout.

### Performance Problem

The fundamental tension: diff indices must participate in Ackermann
constraints (for correctness), but each diff index adds O(n) Ackermann
constraints where n is the existing index set size. With k equivalence
classes needing extensionality, total Ackermann cost grows by O(k·n),
which can dominate for large benchmarks.

The paper's WEG approach avoids this entirely: instead of Ackermann +
diff indices, it uses read-over-weakeq (Lemma 1) which only considers
indices on the path between two arrays, and weakeq-ext (Lemma 2) which
only checks indices in `Stores(P)`.

## Commits

1. `c9f8c2249f` — SMT2 parser: add declare-sort support for QF_AX benchmarks
2. `7b5c78b4de` — Accept member expressions with arbitrary struct operands
3. `f9c542f639` — Simplify expressions after lowering byte operators
4. `89b089d430` — Handle array typecast with different element sizes
5. `200e6d6a9b` — Skip Ackermann constraints for derived arrays
6. `83cb744b41` — Fix array theory: add with-constraints for SSA-renamed indices
7. `405d24568e` — Add extensionality support via Skolem diff indices
8. `eb7b8b63e3` — Add lazy extensionality refinement for --refine-arrays
9. `f338775869` — Inline let bindings in SMT2 parser to fix array theory

## Files Modified

- `src/solvers/smt2/smt2_parser.cpp` — declare-sort support
- `src/solvers/flattening/arrays.cpp` — member expr, typecast, Ackermann skip,
  SSA index fix, extensionality
- `src/solvers/flattening/arrays.h` — extensionality counter and diff index set
- `src/solvers/flattening/boolbv_equality.cpp` — simplify after byte lowering
- `src/solvers/flattening/boolbv_index.cpp` — typecast element size handling
- `regression/smt2_solver/qf_ax_benchmarks/` — 12 QF_AX regression tests
  (10 CORE, 2 KNOWNBUG)
- `regression/cbmc/Array_UF23/` — Ackermann constraint count test (updated)
- `doc/architectural/array-theory-improvements.md` — this document
- `scripts/bench_array_theory.sh` — benchmark runner (not committed)
