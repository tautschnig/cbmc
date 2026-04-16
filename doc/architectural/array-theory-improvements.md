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

### Phase 5: Weak equivalence graph ✅
- Forest-based WEG data structure implemented (primary/secondary edges,
  `get_rep`, `get_rep_mod`, `add_store`, `add_equality`, `path_store_indices`)
- Built alongside existing union-find during `collect_arrays` and
  `record_array_equality`
- **weakeq-ext extensionality (Lemma 2):** replaces Skolem diff indices with
  targeted path-based extensionality. For each array equality `l ↔ (f1 = f2)`,
  collect store indices on the WEG path and assert:
  `∧(f1[i]=f2[i] for i ∈ Stores(path)) → l`
- Falls back to diff indices when path has no store indices (equality edges only)
- **Results:**
  - QF_AX 30s: 500 → 526 correct (90.7% → 95.4%), CPU time -14%
  - QF_AX 180s: **551/551 correct (100%), 0 wrong, 0 timeouts**
  - CBMC regression: 1173/1173 pass, **8% faster** (114s → 105s)
  - Clause reductions: storeinv 88%, swap 48%, storecomm 2-6%
  - Zero overhead on individual CBMC tests (WEG build cost negligible)

#### Failed approaches during WEG development
- **BFS-based WEG:** Simple adjacency list with BFS queries. Worked for
  data structure but `weakly_equivalent_mod` was incorrect (treated equality
  and store edges identically). Led to unsound read-over-weakeq.
- **Read-over-weakeq replacing Ackermann only:** Over-constrains when combined
  with element-wise constraints (together they force arrays to agree at ALL
  indices, which is extensionality — only valid when arrays ARE equal).
- **Read-over-weakeq replacing both element-wise and Ackermann:** Under-constrains
  (missing store axiom `store(a,i,v)[i]=v`). Even with store axiom, extensionality
  diff indices over-constrain because they assume element-wise constraints are present.
- **Forest-based WEG with `make_rep`:** First attempt had infinite loops due to
  edge inversion creating cycles. Fixed with cycle detection guard.
- **Store-index Ackermann skip:** Attempted to skip Ackermann for index pairs
  where both are store indices. Too aggressive — store indices from DIFFERENT
  chains in the same equivalence class still need Ackermann on the base array.
- **Key insight:** weakeq-ext (Lemma 2) is compatible with the existing
  element-wise + Ackermann architecture. It replaces ONLY the extensionality
  encoding, not the constraint generation. This is the correct integration point.

#### Analysis: read-over-weakeq soundness

The soundness issues encountered are NOT fundamental — the paper proves
read-over-weakeq is sound and complete (Lemmas 3 and 4). The issues were
implementation errors:

1. BFS-based `weakly_equivalent_mod` was wrong (fixed by forest-based WEG)
2. Mixing read-over-weakeq WITH element-wise constraints over-constrains
   (usage error — they're alternatives, not complements)
3. Read-over-weakeq WITHOUT element-wise but WITH diff-index extensionality
   over-constrains (diff indices assume element-wise constraints are present)

The correct combination per the paper is:
- Store axiom (idx): `store(a, i, v)[i] = v` ✓
- Read-over-weakeq (Lemma 1): replaces element-wise "else" + Ackermann
- Weakeq-ext (Lemma 2): replaces diff-index extensionality ✓
- array_of/comprehension/if constraints ✓

All components exist. The remaining task is implementing read-over-weakeq
using the forest-based `get_rep_mod` with proper select-term filtering
(only generate constraints for select terms that appear in the formula,
not all (array, index) combinations).

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
| 4. +Inline let bindings | 449 | 0 | 102 | 81.4% | 4863s |
| 5. +Derived-symbol Ackermann skip | **500** | **0** | **51** | **90.7%** | 3450s |
| 6. +WEG weakeq-ext extensionality | **526** | **0** | **25** | **95.4%** | 2978s |

### Definitive results (CaDiCaL 3.0.0)

| Timeout | Jobs | Correct | Wrong | Timeout | Rate |
|---------|------|---------|-------|---------|------|
| 30s | 8 | 500 | 0 | 51 | 90.7% |
| 60s | 8 | 530 | 0 | 21 | 96.1% |
| 120s | 4 | 549 | 0 | 2 | 99.6% |
| 120s | seq | 551 | 0 | 0 | 100% |
| 180s | 8 | **551** | **0** | **0** | **100%** |

All 551 benchmarks solve correctly given sufficient time. The remaining
timeouts at shorter limits are the largest `storecomm` instances (50-60
stores, 2-4.5M clauses) where the SAT solver needs 60-106s.

Standard benchmark configuration: **180s timeout, 8 parallel jobs.**

Key observations:
- Stage 2 halves timeouts (138→68) and reduces CPU time 31% via Ackermann skip
- Stage 3 fixes 166 wrong answers via extensionality; timeouts rise slightly
  (68→101) due to diff index overhead
- Stage 4 fixes all remaining 72 wrong answers via let inlining; zero perf cost
- **Zero wrong answers** in the final stage — theory is complete for all
  benchmarks that finish within the timeout
- 102 remaining timeouts are purely performance (storecomm family dominates)

### Timeout Characterization (Stage 5)

At 30s timeout: 51 remaining timeouts, all `storecomm` family.
At 60s timeout: 21 remaining timeouts, all `storecomm` family.

The 21 remaining timeouts at 60s:
- 9 × `nf_00060` (120 inline stores, 3.3M clauses, solve in ~86s)
- 6 × `sf_00060` (120 named stores, 4.5M clauses, solve in ~106s)
- 1 × `sf_00050` (100 named stores, 3.1M clauses, solve in ~61s)
- 1 × `nf_00050` (100 inline stores, 2.3M clauses, solve in ~61s)
- All expected `unsat`

The bottleneck is **SAT solver time**, not constraint generation (post-processing
completes in ~1s even for the largest). These are genuinely hard SAT instances:
proving that 50-60 stores with all-distinct indices commute requires the SAT
solver to reason about a 2-4.5M clause formula.

### SAT Solver Comparison (CaDiCaL vs MiniSat)

| Benchmark | Stores | Expected | CaDiCaL | MiniSat |
|-----------|--------|----------|---------|---------|
| nf_00010 (unsat) | 20 | unsat | 0.7s | >60s |
| nf_00020 (unsat) | 40 | unsat | 4.8s | >60s |
| nf_00040 (unsat) | 80 | unsat | 18s | >60s |
| nf_00060 (unsat) | 120 | unsat | 86s | >180s |
| sf_00060 (unsat) | 120 | unsat | 106s | >180s |
| invalid_nf_00010 (sat) | 20 | sat | 0.4s | **0.1s** |
| invalid_nf_00050 (sat) | 100 | sat | 14s | **4.4s** |

CaDiCaL is dramatically better for unsat storecomm instances (MiniSat cannot
solve even the smallest within 60s). MiniSat is 3× faster for sat instances.
This suggests the unsat proof requires CDCL techniques that CaDiCaL excels at.

**Transitive derived-symbol detection:** Implemented (follow symbol=symbol
chains to find transitively derived symbols) but did not help the QF_AX
benchmarks since each symbol is directly equated to a store expression.
May help CBMC cases with more complex SSA chains.

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
9. `3372f0fe2f` — Inline let bindings in SMT2 parser to fix array theory
10. `acd6d8f700` — Skip Ackermann for symbols defined as equal to derived arrays
11. `623b83df27` — Add weak equivalence graph and weakeq-ext extensionality
12. `51101b023a` — Skip adding store index to index set (Yices2 optimization)
13. `8bda725cd6` — Replace inner SAT solver with model evaluation in --refine-arrays
14. `717a10681b` — Implement weak congruence in weakeq-ext extensionality
    **BUG:** Over-constrains when store indices overlap (wchains QF_ABV).
    Fixed in commit 23.
15. `862b02995b` — Assumption-based lazy constraints for --refine-arrays
16. `992e3963e0` — Encode read-over-write as bitvector ITE (documentation)
17. `34654ccdac` — Encode read-over-write as bitvector ITE for unbounded arrays
18. `a73d786edf` — Flatten multi-dimensional array index registration
19. `ffc56451ac` — Revert multi-dimensional array index flattening
20. `f68a9c2049` — Inline array-of-arrays definitions for 2D ITE encoding
21. `fe3e968ad1` — Flatten nested arrays as goto-program transformation
22. `ad81adea63` — Update tracking document
23. `ba03e55d20` — Fix unsound weak congruence in weakeq-ext extensionality
24. `376cff3551` — Fix 2D definition inlining crash on SMT2 nested arrays

## Key Architectural Findings

1. **Element-wise constraints are the main bottleneck** (35% of clauses)
   **but cannot be removed.** The ITE encoding handles direct read-over-write
   (`store(a,j,v)[i]` = `ITE(j==i, v, a[i])`) but element-wise constraints
   handle CROSS-ARRAY propagation (connecting reads on different arrays in
   the same equivalence class). Removing element-wise causes 208/551 wrong
   on QF_AX and 3 CBMC failures. The ITE and element-wise are complementary.

2. **The ITE encoding gives 33% CPU speedup** by providing better SAT
   propagation structure (bitvector mux vs conditional clause). It works
   alongside element-wise constraints, not as a replacement.

3. **Ackermann is only needed for pure functional consistency** — arrays
   with ≥2 selects and no store chain. Removing Ackermann entirely passes
   1171/1173 CBMC tests (only Unbounded_Array1 fails).

4. **CaDiCaL's incremental solving works well with assumptions** but poorly
   with permanent clause addition. The assumption-based --refine-arrays
   achieves 100% on QF_AX (was 51.7% with permanent clauses).

5. **Multi_Dimensional_Array6 was a red herring.** It hangs without
   `--unwind 3` on ALL versions (including develop) due to infinite loop
   unwinding, not due to the ITE encoding.

## Performance Progression (QF_AX, 551 benchmarks, 180s, 4 jobs)

| Stage | CPU time | vs baseline |
|-------|----------|-------------|
| Baseline (no changes) | ~4700s | — |
| +All optimizations (pre-ITE) | 2470s | 1.9× faster |
| +ITE encoding | 1642s | 2.9× faster |
| +Weak congruence fix (commit 23) | **2075s** | **2.3× faster** |
| +2D inlining fix (commit 24) | 2070s | 2.3× faster |

## Future Directions

### Multi-dimensional array flattening

CBMC encodes `T[M][N]` as an array of arrays, creating nested store/select
structures: `a[i][j] = v` becomes `store(a, i, store(select(a, i), j, v))`.
This nesting is the root cause of the ITE encoding's inability to replace
element-wise constraints — the ITE handles single-level stores but not the
cross-array propagation needed for nested arrays.

**Implemented:** `flatten_nested_arrays` goto-program pass (commit 21) rewrites
`array(array(T, M), N)` to `array(T, N*M)` with linearized indices `i*M + j`.
Handles stores, reads, array constants, and non-literal array elements.

Key design decisions:
- Top-down pattern matching (bottom-up breaks type consistency)
- 3D+ arrays skipped (partial flattening causes type mismatches)
- `address_of` sub-arrays skipped (pointer arithmetic depends on layout)
- `simplify_expr` NOT called during rewriting (sees inconsistent types)
- Multiplication operands sorted for canonical index form

Results: CBMC 1174/1174, QF_AX 551/551, QF_ABV 0 new wrong answers.
Performance: neutral on CBMC regression suite (106s with and without).
Clause count unchanged for symbolic-dimension arrays (the solver handles
`int a[n*m]` the same as `int a[n][m]`). For constant dimensions, the
back-end ITE encoding already handles the 2D case, so flattening is
redundant.

Remaining gaps (documented, not blocking):
- 3D+ arrays skipped (partial flattening causes type mismatches between
  inner and outer dimensions; need iterative flattening with full tracking)
- `address_of` sub-arrays skipped (pointer arithmetic depends on inner
  array dimension; `&A[i]` stride changes after flattening)
- Counterexample traces show flat indices (e.g., `a[6]` instead of
  `a[1][2]`); would need original dimension metadata in the flattened type
- 2D definition inlining restricted to constant inner sizes (commit 24)
  to prevent crashes on SMT2 nested arrays with symbolic sizes

### References

Remaining gaps:
- 3D+ arrays skipped (partial flattening causes type mismatches between
  inner and outer dimensions; need iterative flattening with full tracking)
- `address_of` sub-arrays skipped (pointer arithmetic depends on inner
  array dimension; `&A[i]` stride changes after flattening)
- Counterexample traces show flat indices (e.g., `a[6]` instead of `a[1][2]`)
- Symbolic multiplication adds clauses for variable-length arrays

- Christ, Hoenicke: "Weakly Equivalent Arrays" (arXiv:1405.6939, FroCos 2015)
- Irfan, Graham-Lengrand: "Arrays Reasoning in MCSat" (SMT 2024)
  — Yices2 MCSat array integration using WEG
- Niemetz, Preiner: "Bitwuzla" (CAV 2023, LNCS 13965)
  — Lemmas-on-demand architecture, bit-vector abstraction of arrays
- Niemetz, Preiner, Zohar: "Scalable Bit-Blasting with Abstractions"
  (CAV 2024, LNCS 14681) — CEGAR for bit-vector arithmetic

## SAT Solver Comparison

| Config | QF_AX (551) | CPU time |
|--------|-------------|----------|
| CaDiCaL eager | 551/551 | 2470s |
| CaDiCaL + refine (assumptions) | 551/551 | 3889s |
| CaDiCaL + refine (permanent clauses) | 285/551 | — (hung) |
| MiniSat eager | 526/551 | 8929s |
| MiniSat + refine | 522/551 | 10808s |

CaDiCaL is 3.6× faster than MiniSat on eager solving. MiniSat can't solve
the hardest storecomm benchmarks. CaDiCaL's incremental re-solve was slow
with permanent clause addition but works well with assumptions.

CaDiCaL's `constrain` API (temporary clause for one solve) is too limited
for our use case (only one clause at a time, designed for IC3).

#### Read-over-weakeq as Ackermann replacement (attempted, not landed)

Attempted replacing Ackermann with read-over-weakeq (Lemma 1). Multiple
approaches tried:

1. **Unconditional read-over-weakeq** (a ≈ᵢ b → a[i]=b[j]): UNSOUND.
   Over-constrains sat instances because the static `weakly_equivalent_mod`
   check doesn't account for the runtime path condition.

2. **Path-conditioned read-over-weakeq** (Cond_i(path) ∧ i=j → a[i]=b[j]
   where Cond_i includes i≠k for each store index k on the path): SOUND
   but INCOMPLETE. For storecomm benchmarks where read indices ARE store
   indices, the condition i≠k₁∧...∧i≠kₙ is always false, making the
   constraint vacuously true. The paper handles this via **weak congruence**
   (Definition 3), which chains through store values when the read index
   equals a store index. Without weak congruence, read-over-weakeq cannot
   replace element-wise constraints.

3. **Ackermann + cross-array read-over-weakeq**: Adding cross-array
   constraints on top of element-wise + Ackermann is pure overhead (the
   element-wise constraints already handle cross-array propagation).
   Generated 1.17M redundant constraints on address_space_size_limit3.

**Key finding:** Read-over-weakeq (Lemma 1) requires weak congruence
(Definition 3) to be complete. Weak congruence was implemented but is
still incomplete for storecomm benchmarks: the per-edge condition
`beforeₘ[i] = afterₘ[i]` doesn't chain through multiple stores
correctly. The paper's Definition 3 uses an existential (`∃a'b'`) that
requires finding the right intermediate arrays for each store index,
which is complex in a bit-blasting architecture.

**Architectural conclusion:** In CBMC's bit-blasting architecture:
- **Element-wise constraints** = propositional encoding of read-over-write
  (handles cross-array propagation through store chains)
- **Ackermann on base arrays** = functional consistency for uninterpreted
  arrays (`i=j → a[i]=a[j]`)
- **Derived-array Ackermann skip** = propositional equivalent of
  read-over-weakeq (derived arrays don't need Ackermann because
  element-wise constraints already propagate through the store chain)
- **weakeq-ext extensionality** = Lemma 2 from the paper

Verified: removing Ackermann entirely passes 1171/1173 CBMC tests.
The 2 failures are Array_UF23 (count test) and Unbounded_Array1 which
tests exactly `i=j → a[i]=a[j]` — pure functional consistency that
only Ackermann provides. Element-wise constraints handle everything else.

The current architecture IS the correct propositional encoding of the
paper's approach. No further changes needed.

#### --refine-arrays evaluation

Tested using `bv_refinementt` with `refine_arrays=true` in smt2_solver:
- QF_AX: 285/551 correct, 0 wrong, 266 timeout (51.7%) — much worse
  than eager (551/551). Sat instances get stuck in the refinement loop
  because CaDiCaL's incremental solving is slow after adding clauses.
- CBMC: 1173/1173 pass, 104s — same as eager (105s). CBMC formulas are
  mostly unsat so the refinement converges in 1-2 iterations.

Improvements applied:
- Replaced inner SAT solver checks with direct model evaluation
  (Bitwuzla-style): no performance change because the bottleneck is
  the SAT re-solve, not the constraint checking.
- Added max-activations bound per iteration (Yices2-style).

The refinement approach is sound but not beneficial for QF_AX benchmarks.
For CBMC, it's neutral. The eager approach with derived-symbol skip
remains the better default.

#### Yices2/Bitwuzla analysis (SMT-COMP 2024 winners)

**Yices2** (QF_AX winner, CDCL(T)):
- Uses E-graph with array extensions, WEG for conflict detection
- Key optimizations: "may conflict" filtering, max_update_conflicts
  bound, stratified extensionality, separation of update conflicts
  and extensionality
- "May conflict" filter: attempted in CBMC but unsound — the pre-merge
  index count doesn't reliably indicate which arrays have direct selects

**Bitwuzla** (QF_ABV winner):
- No WEG. Model-guided DAG traversal with lazy lemma generation.
- Bidirectional traversal (down through stores, up through parents)
- Path condition collection for lemma generation
- No Ackermann — congruence conflicts detected lazily

**Key insight:** The bottleneck for `--refine-arrays` is CaDiCaL's
incremental SAT re-solve performance, not the constraint checking.
Both Yices2 and Bitwuzla use native theory solvers that avoid this
issue entirely. CBMC's bit-blasting architecture fundamentally limits
the effectiveness of lazy approaches.

## CBMC Performance Impact

Full CBMC regression suite (1173 tests, 60s timeout):
- Without WEG (diff-index extensionality): 114s
- With WEG (weakeq-ext extensionality): **105s (8% faster)**

Individual array-heavy tests show negligible overhead from WEG construction:
- Array_UF8: 37ms → 36ms
- Array_operations4: 66ms → 66ms
- bounds_check1: 4410ms → 4379ms

The WEG-based weakeq-ext is a pure win: same or better performance on
standard CBMC benchmarks, with correct extensionality when needed.

## QF_ABV Benchmark Results

15,148 benchmarks from SMT-LIB 2025 (arrays + bitvectors). Tested 1,000
across multiple families.

### Wrong answers (44 total — soundness bug in weak congruence)

All 44 wrong answers return `unsat` when `sat` expected. Root cause: the
weak congruence implementation (commit 14, `717a10681b`) over-constrains
when store indices overlap. Bisection confirmed: develop returns `sat`
(correct), the regression starts at that commit.

| Family | Wrong | Pattern |
|--------|-------|---------|
| wchains*se | 43 | Write chain permutations with overlapping byte indices |
| matrixmultcomm | 1 | Matrix multiplication commutativity |

See "Weak congruence soundness bug" section for analysis.

### Errors (227 → 0, fixed in commit 24)

All errors are invariant violations in `bv_utils.cpp:99`:
`a.size() == b.size()` precondition failure (bitvector width mismatch).

| Family | Errors | Notes |
|--------|--------|-------|
| UltimateAutomizer | ~50 | Complex multi-sort formulas |
| cs_* (concurrency) | ~20 | Dekker, Peterson, Lamport, etc. |
| kbfiltr, parport, s3* | ~60 | Device driver verification |
| 20200415-Yurichev | ~60 | Reverse engineering formulas |
| Other | ~37 | Various |

Root cause: 2D definition inlining applied to SMT2 nested arrays with
symbolic inner sizes, causing width mismatch in bv_utils::select.
Fixed by restricting 2D definitions to constant inner sizes (commit 24).
These benchmarks now run out of memory (same as develop) instead of crashing.

### Timeouts (107 total — performance)

Large formulas where the SAT solver needs more than 60s. Not correctness
issues.

### Correct (622 of 1000 tested)

| Family | Tested | Correct | Wrong | Timeout | Error |
|--------|--------|---------|-------|---------|-------|
| 2018-Mann (egt) | ~200 | ~200 | 0 | 0 | 0 |
| 20200415-Yurichev | ~100 | ~40 | 0 | 0 | ~60 |
| brummayerbiere | ~200 | ~90 | 44 | ~30 | ~36 |
| UltimateAutomizer | ~100 | ~10 | 0 | ~40 | ~50 |
| Other families | ~400 | ~282 | 0 | ~37 | ~81 |

### Weak congruence soundness bug

**Symptom:** 44 QF_ABV benchmarks return `unsat` when `sat` expected.

**Minimal example (wchains002se):** Two store chains writing 4 bytes each
at addresses `v6..v6+3` and `v7..v7+3` to the same base array, in different
order. The benchmark asserts the chains are NOT equal. Expected: `sat`
(they differ when `v6 == v7` because the last store wins differently).

**Root cause:** The weak congruence path condition for weakeq-ext
extensionality generates conditions like `before[i] = after[i]` for each
store edge on the WEG path. When store indices overlap (e.g., `v6 == v7`),
these conditions incorrectly force array equality by not accounting for
the fact that the "last store wins" semantics differs between the two
chains.

**Bisection:** develop → `sat` (correct). Commit `717a10681b` → `unsat`
(wrong). All prior commits → `sat` (correct).

**Status:** Fixed in commit 23. The weak congruence optimization was removed
from weakeq-ext extensionality. The correct condition per Lemma 2 is
`f1[k] = f2[k]` for all store indices k. Weak congruence (Definition 3)
applies only to read-over-weakeq (Lemma 1), not extensionality.

The fix costs 27% QF_AX CPU time (2075s vs 1635s) because comparing full
store chain expressions `f1[k]` and `f2[k]` is harder for the SAT solver
than comparing intermediate sub-expressions. This is the cost of correctness.

**Lesson learned:** The weak congruence condition `after_a[k] = after_b[k]`
is WEAKER than `f1[k] = f2[k]` (easier to satisfy), which makes the
extensionality clause fire MORE often. This is unsound because it proves
array equality even when the final arrays differ. The correct condition
`f1[k] = f2[k]` is STRONGER (harder to satisfy), making extensionality
fire only when the arrays truly agree at all store indices.

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
