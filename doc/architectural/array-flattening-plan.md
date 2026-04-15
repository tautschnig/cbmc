# Multi-Dimensional Array Flattening — Detailed Plan

## Problem

CBMC encodes `T a[N][M]` as `array(array(T, M), N)`. The SSA for
`a[i][j] = v` produces:

```
a#2 = with(a#1, i, with(a#1[i], j, v))
```

This nested structure creates:
- Array-of-arrays in the union-find (nested equivalence classes)
- Nested store chains that the ITE encoding can't flatten
- Element-wise constraints at TWO levels (outer and inner)
- Cross-level propagation complexity

## Goal

Flatten `array(array(T, M), N)` to `array(T, N*M)` in the solver
back-end. The SSA remains unchanged; flattening is transparent.

After flattening, `a[i][j] = v` becomes:

```
a_flat#2 = with(a_flat#1, i*M + j, v)
```

Single-level store. ITE encoding handles it directly.

## Detailed Design

### 1. Detection: when to flatten

In `convert_bv` or `convert_index`, when we encounter an array type
whose element type is also an array type:

```
array_typet outer: element = array_typet inner: element = T, size = M
                   size = N
```

Flatten to: `array_typet flat: element = T, size = N*M`

Only flatten when BOTH dimensions are unbounded (handled by array theory).
If either dimension is bounded (flattened to bitvectors), no change needed.

### 2. Index linearization

For `index_exprt(a, i)` where `a` has type `array(array(T, M), N)`:
- Don't return an array-typed result
- Record that `a[i]` maps to slice `(a_flat, i, M)` meaning
  "elements i*M through i*M + M-1 of a_flat"

For `index_exprt(index_exprt(a, i), j)` (the actual element access):
- Recognize that the inner `index_exprt(a, i)` is a slice
- Compute linearized index: `i * M + j`
- Return `index_exprt(a_flat, i * M + j)` — single-level access

### 3. Index normalization

To ensure syntactic equality of equivalent index expressions:

**Canonical form for linearized indices:** `base * stride + offset`

Normalization rules:
- `i * M + j` → canonical (already normalized)
- `j + i * M` → rewrite to `i * M + j` (multiplication before addition)
- `M * i + j` → rewrite to `i * M + j` (sort multiplication operands
  by expression complexity: symbols before constants, alphabetical)
- `(i * M + j) + k` → `i * M + (j + k)` (associate additions right)

Implementation: a `normalize_linear_index(exprt)` function that:
1. Collects all additive terms
2. Separates multiplicative terms (containing the stride) from offsets
3. Sorts multiplicative terms by: stride value, then variable name
4. Reconstructs as `sum_of(var_k * stride_k) + sum_of(offsets)`

For nested flattening (3D+ arrays), the canonical form extends:
`i * (M * K) + j * K + k` — strides are products of inner dimensions,
sorted by decreasing stride.

### 4. Store flattening

For `with(a, i, with(a[i], j, v))` where `a` is array-of-arrays:

Recognize the pattern:
- Outer with: `with(a, i, new_inner)` where `new_inner` is a with on `a[i]`
- Inner with: `with(a[i], j, v)`

Flatten to: `with(a_flat, normalize(i * M + j), v)`

This pattern recognition happens in `collect_arrays` or `convert_bv`.

### 5. Read flattening

For `a#3[1][0]` (which is `index_exprt(index_exprt(a#3, 1), 0)`):

In `convert_index`:
- Outer: `index_exprt(a#3, 1)` — detect array-of-arrays, record slice
- Inner: `index_exprt(slice, 0)` — compute `1 * M + 0`, access `a_flat#3`

### 6. Counterexample trace mapping

In `boolbv_get` / `bv_get_rec`, when reconstructing values for
array-of-arrays types:
- Read from `a_flat` at linearized indices
- Reconstruct the nested array structure for display

### 7. Implementation location

All changes in the solver back-end:
- `src/solvers/flattening/boolbv_index.cpp` — index linearization
- `src/solvers/flattening/boolbv_with.cpp` — store flattening
- `src/solvers/flattening/arrays.cpp` — collect_arrays for flat arrays
- `src/solvers/flattening/boolbv_get.cpp` — trace reconstruction
- New: `src/solvers/flattening/flatten_array.h` — normalization utilities

### 8. Risks and mitigations

- **Symbolic dimensions:** `i * M` where M is symbolic requires bitvector
  multiplication. Mitigation: only flatten when M is constant, or accept
  the multiplication cost (one mult per access vs nested array overhead).

- **Partial updates:** `a[i] = new_inner_array` (replacing an entire row)
  becomes M individual stores on `a_flat`. Mitigation: detect this pattern
  and use a loop or array_of construct.

- **Array equality:** `a[i] == b[i]` comparing inner arrays becomes
  element-wise comparison of M elements. Mitigation: this is already
  what the solver does for bounded inner arrays.

- **Interaction with existing array theory:** The flattened array still
  goes through the array theory (union-find, Ackermann, extensionality).
  The benefit is that it's single-level, so the theory is simpler.

## Implementation Attempt: Compound ITE in convert_index

Attempted flattening by intercepting `index_exprt(index_exprt(a, i), j)`
in `convert_index` and walking the store chain with compound conditions
`(outer_idx == k && inner_idx == l)` instead of nested ITEs.

**Results:**
- Correctness: 1173/1173 CBMC pass ✓
- Simple 2D (4 stores): 6711 → 6063 clauses (10% reduction) ✓
- Complex 2D (Multi_Dimensional_Array6): 73405 → 81162 clauses (11% increase) ✗

**Root cause of increase:** The base case (symbol array) creates free
variables for the inner array read, which the array theory then constrains
with element-wise constraints. These additional constraints outweigh the
savings from the compound ITE.

**Conclusion:** The flattening cannot be done purely in `convert_index`.
It needs to rewrite the expression tree so that the base array symbol has
a flat type from the start. This requires either:
1. A preprocessing pass before the solver that rewrites array-of-arrays
   expressions to flat array expressions, or
2. Changes in goto-symex to produce flat SSA for multi-dimensional arrays.

Both approaches are substantial projects that should be tackled separately.

## Analysis: Why Back-End-Only Flattening Is Hard

The ITE encoding already handles the INNER level of 2D arrays efficiently:
`(a[i])[j]` where `a[i]` is a store chain generates compound ITEs. The
overhead comes from the OUTER level: the array theory creates an equivalence
class for the array-of-arrays, generates element-wise constraints for
array-typed equalities (`a[i] = old[i]` where both sides are inner arrays),
and runs Ackermann/extensionality on the outer level.

Attempts to flatten in the back-end failed because:
1. `boolbv_width` returns 0 for unbounded inner array types — can't create
   bitvectors for intermediate array-typed results
2. The array theory is the only component that handles unbounded intermediate
   types (via free variables + element-wise constraints in the union-find)
3. Creating free variables for the element type directly disconnects them
   from the array theory's constraints, requiring duplicate constraint sets

The correct approach: rewrite the expressions BEFORE they enter the array
theory, changing `with(a, i, with(a[i], j, v))` to `with(a_flat, i*M+j, v)`
with a flat array type. This eliminates the intermediate array-typed level
entirely, so the array theory only sees single-level operations.

This rewrite can be done as a preprocessing pass in `finish_eager_conversion`
before `add_array_constraints`, or in `set_to`/`convert` when expressions
are first seen. The key requirement: ALL references to the array must be
rewritten consistently (stores, reads, equalities, let-bindings).

## Implementation Attempt: Coordinated Back-End Flattening

Attempted coordinated flattening in `collect_indices` (reads) and
`collect_arrays` (stores) using shared `linearize_2d_read` and
`linearize_2d_index` helpers with normalized index computation.

**Results:**
- Skipping outer element-wise constraints for 2D stores gives massive
  clause reduction: 6711 → 1547 (77%) and 73405 → 6894 (91%)
- BUT the store-read connection breaks because SSA symbols hide the
  `with` expression: `a#2[1][0]` where `a#2` is a symbol defined as
  `with(a#1, 1, with(a#1[1], 0, 42))`. The ITE encoding sees the
  symbol, not the `with` definition.
- Element-wise constraints are needed to connect SSA symbols to their
  definitions. Skipping them breaks this connection.

**Root cause:** The bitvector encoding (`convert_index`) sees SSA symbols,
not their definitions. The array theory's element-wise constraints bridge
this gap. The 2D flattening can't bypass element-wise without also
resolving SSA symbols to their definitions.

**Conclusion:** 2D flattening requires an expression-level preprocessing
pass that resolves SSA symbol definitions and rewrites the expression tree
BEFORE the solver sees it. This is a goto-symex or preprocessing change,
not a solver back-end change.

## Implementation Attempt: Goto-Program Transformation

Implemented `flatten_nested_arrays` as a goto-program pass (like
`remove_vector`). For constant inner dimensions, it successfully
produced flat SSA: `a[0][0] = 0` became `a[0] = 0` (with `0*3+0`
simplified to `0`).

**Results:**
- Constant inner dims: correct, very compact encoding (751 vars, 1160 clauses)
- Symbolic inner dims: multiplication overhead makes it worse (25K vs 6.7K)
- 3 CBMC regression failures from type inconsistencies

**Root cause of failures:** The type rewriting is incomplete — some
expression nodes retain the nested array type while others have the
flat type, causing simplifier invariant violations. A thorough
implementation needs to rewrite types in ALL expression nodes, symbol
table entries, and type annotations, similar to how `remove_vector`
handles vector types.

**Conclusion:** The goto-program approach works for constant inner
dimensions but needs more thorough type rewriting. The back-end
approach (definition inlining + 2D ITE) is the safe choice for now,
giving 18-39% reduction. The goto-program approach is deferred for
future work with proper type system handling.
