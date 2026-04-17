# Plan: Truly Lazy Array Theory for CBMC

## Goal

Close the performance gap between CBMC's bit-blasting array solver and
CDCL(T) solvers like Yices2 (currently 30-500× slower on QF_AX).

## Current Architecture

```
Formula → convert() → bit-blast everything → add_array_constraints() → SAT solve
                         ↑                        ↑
                    ITE encoding            element-wise + Ackermann
                    (per select)            (quadratic in indices)
```

Every array select `a[i]` is immediately bit-blasted into an ITE chain
walking the store chain. Every pair of indices in the same equivalence
class gets an Ackermann constraint. The SAT solver works on a formula
with all array semantics fully encoded as propositional clauses.

Yices2's architecture:

```
Formula → E-graph → SAT solve → theory check → lemma → re-solve
                                     ↑              ↑
                              constant-time     single clause
                              model lookup      (targeted)
```

The theory solver never bit-blasts. It checks the model in the E-graph
(constant time per check) and generates one clause per violation.

## The Three Bottlenecks

### 1. Eager bit-blasting of selects (biggest cost)

Every `a[i]` is converted to `ITE(i==k_n, v_n, ITE(i==k_{n-1}, ...))`.
For a store chain of length n with w-bit elements, this creates O(n·w)
SAT variables and O(n·w) clauses. For 10 stores on 32-bit arrays:
~3200 clauses per select.

**Yices2 equivalent:** Zero clauses. The E-graph stores `a[i]`'s value
directly. When the SAT solver needs to know if `a[i] == v`, the theory
solver answers from the E-graph in O(1).

### 2. Quadratic Ackermann constraints

For n indices in an equivalence class, we generate O(n²) constraints:
`i₁ == i₂ → a[i₁] == a[i₂]`. For 40 indices: 780 constraints, each
with ~w clauses for the element equality.

**Yices2 equivalent:** Congruence lemmas on demand. When the E-graph
merges two index terms, it checks if any array selects become
congruent. Only generates a lemma when the model violates congruence.
Typically 0-5 lemmas total.

### 3. Element-wise constraints for stores

For each store `x = store(y, j, v)` and each index i in the index set:
`j ≠ i → x[i] = y[i]`. This is O(n) per store, O(n·m) total for m
stores.

**Status:** Already eliminated by the element-wise skip (commit 26).
The ITE encoding handles this. No further work needed.

## Phased Plan

### Phase A: Lazy Ackermann (medium effort, high impact)

**Goal:** Replace quadratic eager Ackermann with on-demand congruence.

**Current cost:** For storecomm_00060 with 120 stores and ~60 indices:
~1800 Ackermann constraints × ~100 clauses each = ~180K clauses.

**Approach:**
1. In `add_array_Ackermann_constraints()`, when `lazy_arrays` is true,
   skip all Ackermann constraint generation.
2. In `arrays_overapproximated()`, after evaluating the model, check
   for Ackermann violations: for each pair of indices (i₁, i₂) in the
   same equivalence class, if `get_value(i₁) == get_value(i₂)` but
   `get_value(a[i₁]) != get_value(a[i₂])`, add the single constraint
   `i₁ == i₂ → a[i₁] == a[i₂]`.
3. Only check index pairs where both indices have the same model value
   (use a hash map: value → list of indices).

**Expected impact:** Eliminate ~180K clauses on storecomm_00060. The
refinement loop adds only the violated constraints (typically 0-10).

**Risk:** Low. Ackermann is a pure consistency check — deferring it
can only cause spurious SAT (caught by refinement), never spurious
UNSAT.

**Prerequisite:** The truly lazy constraint infrastructure (commit 29).

### Phase B: Lazy select bit-blasting (high effort, highest impact)

**Goal:** Don't bit-blast `a[i]` until the refinement loop needs its
value.

**Current cost:** Each select creates an ITE chain with O(n·w) clauses.
This is the dominant cost — for storecomm_00060, the ITE chains for
~60 selects on ~120-store chains produce millions of clauses.

**Approach:**
1. In `convert_index()`, when `lazy_arrays` is true and the array is
   in the array theory (not a small flattened array), return fresh
   unconstrained bitvector variables instead of the ITE chain.
2. Record the mapping: `{fresh_bv, index_exprt}` in a side table.
3. In the refinement loop, evaluate the model: for each recorded
   select, compute what the value SHOULD be (by walking the store
   chain with model values for indices) and compare with the SAT
   model's value for the fresh variables.
4. If they disagree, add the ITE constraint for that specific select:
   `fresh_bv == ITE(i==k_n, v_n, ITE(...))`.

**Key insight:** Most selects in a formula are never the cause of a
conflict. The SAT solver can find a satisfying assignment for the
non-array part, and only a few selects need their ITE chains to
resolve conflicts.

**Challenge:** The model evaluation in step 3 requires walking the
store chain with concrete index values. This is what Bitwuzla's
`check_access` does (the bidirectional DAG traversal). We need to
implement this model-based store chain evaluation.

**Implementation sketch:**
```cpp
// In convert_index, for lazy mode:
bvt lazy_bv = prop.new_variables(width);
lazy_selects.push_back({lazy_bv, expr});
return lazy_bv;

// In refinement loop:
for(auto &sel : lazy_selects) {
  exprt expected = evaluate_select_in_model(sel.expr);
  exprt actual = bv_get(sel.bv, sel.expr.type());
  if(expected != actual) {
    // Bit-blast the ITE chain and equate with lazy_bv
    bvt ite_bv = convert_index_eager(sel.expr);
    for(size_t i = 0; i < width; i++)
      prop.lcnf(!sel.bv[i], ite_bv[i]);  // lazy[i] == ite[i]
      prop.lcnf(sel.bv[i], !ite_bv[i]);
    progress = true;
  }
}
```

**Expected impact:** For sat instances, most selects never need
bit-blasting → dramatic clause reduction. For unsat instances, all
selects eventually get bit-blasted → same total clauses but spread
across iterations (incremental solving may help or hurt).

**Risk:** Medium. The model evaluation must correctly handle:
- Nested stores with symbolic indices
- Store chains across equality edges
- ITE arrays (conditional stores)
The Bitwuzla source code provides a reference implementation.

**Prerequisite:** Phase A (lazy Ackermann) should be done first to
avoid generating quadratic constraints for the fresh variables.

### Phase C: Lazy equality constraints (medium effort, medium impact)

**Goal:** Defer `(a == b) → a[i] == b[i]` constraints.

**Current cost:** For each equality literal and each index in the
equivalence class: one implication clause. O(e·n) where e is the
number of equalities and n is the index set size.

**Approach:**
1. Skip equality constraint generation when `lazy_arrays` is true.
2. In the refinement loop, check: for each equality `a == b` that is
   true in the model, verify that `get_value(a[i]) == get_value(b[i])`
   for all indices i. If not, add the single violated constraint.

**Expected impact:** Moderate. Equality constraints are typically fewer
than Ackermann constraints.

**Risk:** Low. Same reasoning as Phase A.

### Phase D: Incremental index discovery (low effort, enables A-C)

**Goal:** Don't pre-compute the full index set. Discover indices as
the refinement loop encounters them.

**Current:** `collect_indices()` walks all array expressions and
collects every index that appears. This determines the Ackermann
constraint count.

**Approach:**
1. Start with an empty index set.
2. When the refinement loop bit-blasts a select `a[i]` (Phase B),
   add `i` to the index set.
3. When a new index is added, check for Ackermann violations against
   existing indices (Phase A).

**Expected impact:** Reduces the index set to only indices that are
actually needed, which reduces Ackermann from O(n²) to O(k²) where
k << n is the number of indices actually involved in conflicts.

### Phase E: Theory propagation in SAT solver (very high effort)

**Goal:** Integrate array theory checks into the SAT solver's
propagation loop, eliminating the solve-check-add-resolve cycle.

**Approach:** This requires modifying CaDiCaL (or using a different
SAT solver) to support theory propagation callbacks:
1. After each decision/propagation, call the array theory checker.
2. The checker evaluates the partial assignment against array axioms.
3. If a violation is found, generate a conflict clause and backtrack.

This is the full CDCL(T) approach that Yices2 uses. It eliminates
the overhead of complete SAT solves between refinement iterations.

**Expected impact:** Would close most of the remaining gap to Yices2.
The per-check cost is O(1) (E-graph lookup) vs O(n) (SAT re-solve).

**Risk:** Medium-high. Requires implementing the `ExternalPropagator`
interface and maintaining array theory state that tracks the SAT
solver's partial assignment. The propagator must handle backtracking
correctly (undo theory state when the SAT solver backtracks).

**CaDiCaL support confirmed:** CBMC's CaDiCaL version already has the
`ExternalPropagator` API with:
- `notify_assignment(lits)` — track index/element assignments
- `notify_backtrack(level)` — undo theory state
- `cb_check_found_model(model)` — full model check (like current refinement)
- `cb_propagate()` — theory propagation (return implied literal)
- `cb_add_reason_clause_lit()` — explain propagation

**Implementation sketch:**
```cpp
class array_propagatort : public CaDiCaL::ExternalPropagator {
  // Track assignments to index and element variables
  void notify_assignment(const vector<int> &lits) override {
    for(int lit : lits) {
      if(is_index_var(lit)) update_index_assignment(lit);
      if(is_element_var(lit)) update_element_assignment(lit);
    }
  }
  // Check for congruence violations
  int cb_propagate() override {
    // If two indices are assigned equal but their selects differ,
    // propagate the equality of the selects
    for(auto &[idx_pair, status] : watched_pairs) {
      if(indices_equal(idx_pair) && selects_differ(idx_pair))
        return select_equality_lit(idx_pair);
    }
    return 0;
  }
  bool cb_check_found_model(const vector<int> &model) override {
    // Full array theory check (store axioms, extensionality)
    return check_all_array_axioms(model);
  }
};
```

## Priority and Dependencies

**Alternative:** Use CaDiCaL's `connect_external_propagator` API
(added in CaDiCaL 2.0) which provides external propagation callbacks.
This is designed exactly for CDCL(T) integration. Need to verify
CBMC's CaDiCaL version supports this.

```
Phase A (lazy Ackermann)     ← low risk, high impact, do first
  ↓
Phase D (incremental indices) ← enables smaller index sets
  ↓
Phase B (lazy selects)        ← highest impact, needs A+D
  ↓
Phase C (lazy equalities)     ← moderate impact, easy after B
  ↓
Phase E (theory propagation)  ← closes the gap, needs SAT solver work
```

## Expected Performance Progression

| Phase | storecomm_00060 | vs Yices2 | Notes |
|-------|-----------------|-----------|-------|
| Current (eager) | 5.2s | 130× | All constraints upfront |
| +A (lazy Ackermann) | ~2s | ~50× | Eliminate quadratic cost |
| +B (lazy selects) | ~0.5s | ~12× | Eliminate ITE chains |
| +C (lazy equalities) | ~0.3s | ~7× | Eliminate equality propagation |
| +E (theory propagation) | ~0.1s | ~2× | Eliminate re-solve overhead |
| Yices2 | 0.04s | 1× | Native CDCL(T) |

These estimates are speculative. The actual speedups depend on how many
constraints the refinement loop needs to add (benchmark-dependent).

## Measuring Progress

Use the 6 selected QF_AX benchmarks as the primary metric:
- storeinv_00002 (small unsat, needs extensionality)
- storeinv_00010 (medium unsat, needs extensionality)
- swap_00010 (sat)
- storecomm_00010 (small unsat)
- storecomm_00040 (sat)
- storecomm_00060 (large unsat)

Plus the full QF_AX suite (551 benchmarks, 180s) for correctness.

## References

- CaDiCaL external propagator: `connect_external_propagator()` in
  CaDiCaL 2.0+ (Biere, Fazekas, Fleury, Heisinger, 2020)
- Bitwuzla array solver: `src/solver/array/array_solver.cpp`
  (check_access, collect_path_conditions)
- Yices2 fun_solver: `src/solvers/funs/fun_solver.c`
  (update_conflict_for_application, reconcile_model)
