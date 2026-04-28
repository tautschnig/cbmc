/// \file
/// Array Theory Encoding Architecture
///
/// This document describes the array theory encoding in CBMC's propositional
/// back-end, including the lazy select optimization (Phase B) and the
/// refinement loop integration.
///
/// # Overview
///
/// The array theory translates array operations (select, store, equality)
/// into propositional logic. The encoding has three layers:
///
/// 1. **ITE encoding** (boolbv_index.cpp): Translates `store(a,i,v)[j]` into
///    `ITE(i==j, v, a[j])` — a bitvector multiplexer.
///
/// 2. **Equality constraints** (arrays.cpp): For each array equality
///    `a == b`, generates `l → a[k] == b[k]` for each index `k`.
///
/// 3. **Ackermann constraints** (arrays.cpp): For each pair of indices
///    `(i,j)` on the same array, generates `i == j → a[i] == a[j]`.
///
/// # Lazy Selects (Phase B)
///
/// When `lazy_arrays` is true (enabled in the smt2_solver), `convert_index`
/// returns free bitvector variables instead of ITE chains. This defers the
/// expensive ITE encoding until the refinement loop determines it's needed.
///
/// ## Refinement Loop (refine_arrays.cpp)
///
/// The refinement loop in `arrays_overapproximated()` has these phases:
///
/// **Phase 1 — Collect violations (model queries only):**
/// - 1a. Collect with-selects for bit-blasting
/// - 1b. Evaluate lazy constraints (force-activate all true-guard constraints)
/// - 1c. Detect Ackermann violations (equal indices, different values)
/// - 1d. Detect extensionality candidates (arrays claimed equal but differ)
///
/// **Phase 2 — Add clauses (model invalidated):**
/// - 2a. Bit-blast with-selects (erase bv_cache, re-convert with ITE encoding)
/// - 2b. Activate lazy constraints (convert and assert)
/// - 2c. Add Ackermann constraints
/// - 2d. Add extensionality constraints (with equality constraints for diff index)
///
/// The `lazy_arrays` flag is set to `false` during Phase 2 to prevent
/// creating new lazy selects during constraint activation.
///
/// ## Force-Activation
///
/// With lazy selects, all model values are free (self-consistent). The
/// refinement loop can't detect violations by comparing model values.
/// Instead, ALL lazy constraints where the guard is true are force-activated
/// in every iteration. This ensures the ITE encoding connects free BVs to
/// the store chain.
///
/// ## Index Registration
///
/// When creating a lazy select on a store chain, indices for intermediate
/// arrays are also registered (boolbv_index.cpp). Without this, the array
/// theory has fewer indices and may not generate enough Ackermann constraints.
///
/// # Weak Equivalence Graph (WEG)
///
/// The WEG (arrays_weg.h) tracks relationships between arrays:
/// - **Store edges**: `b = store(a, i, v)` — `b` is `a` with index `i` updated
/// - **Equality edges**: `a == b` — arrays are asserted equal
///
/// The WEG is used for:
/// - **Ackermann skip**: Skip Ackermann constraints between arrays that are
///   weakly equivalent modulo the read index (read-over-weakeq).
/// - **Extensionality**: Only compare arrays at store indices on the WEG path
///   (weakeq-ext).
/// - **Derived symbol detection**: Skip Ackermann for symbols equated to
///   derived arrays (stores, ifs, etc.).
///
/// **Important**: `weakly_equivalent_mod` uses syntactic index comparison.
/// It's safe as an optimization (skip definitely-unneeded constraints) but
/// cannot be used as a soundness filter.
///
/// # Let Handling
///
/// ## Parser (smt2_parser.cpp)
///
/// The iterative let accumulation path creates `let_exprt` for all let
/// frames (both BV-typed and array-typed bindings). This avoids the
/// exponential expression growth from eager let expansion.
///
/// ## Solver (boolbv_let.cpp)
///
/// `convert_let` uses a **scope stack** for BV-typed bindings: original
/// symbols are mapped to their BVs via the scope stack, which is checked
/// before `bv_cache` in `convert_bv`. This eliminates the expensive
/// `replace_symbolt` traversal of the where-expression.
///
/// For array-typed bindings, fresh symbols are created and mapped
/// persistently (for the array theory via `record_array_let_binding`).
/// The `replace_symbolt` is only used for binding VALUES (small
/// expressions), not for the where-expression.
///
/// ## replace_symbolt Optimization (replace_symbol.cpp)
///
/// The `have_to_replace(dest)` pre-check before recursive replacement
/// has been removed. This pre-check traversed the entire expression tree
/// to determine if any symbol needed replacement, then `replace()`
/// traversed again. For deeply nested `let_exprt`, this caused O(k²)
/// traversals. The optimized `replace()` handles symbols directly and
/// recurses into operands without the pre-check.
///
/// # CaDiCaL Configuration
///
/// CaDiCaL's congruence closure (gate detection and rewriting) is disabled.
/// It consumed 62% of solving time on array-heavy formulas by trying to
/// detect ITE/AND/OR gate structures that are already optimal.
