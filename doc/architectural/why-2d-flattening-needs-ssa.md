# Why 2D Array Flattening Requires SSA-Level Changes

## The Problem

CBMC encodes `int a[N][M]` as `array(array(int, M), N)`. The SSA for:

```c
a[1][0] = 42;
x = a[1][0];
assert(x == 42);
```

produces:

```
a#2 = with(a#1, 1, with(a#1[1], 0, 42))   // store
x#2 = a#2[1][0]                             // read
assert x#2 == 42
```

## The SSA Symbol Barrier

The read `a#2[1][0]` is the expression tree:

```
index_exprt(
  index_exprt(a#2, 1),    // inner: read row 1 from a#2
  0)                       // outer: read element 0 from that row
```

Critically, `a#2` is an **SSA symbol** — an opaque identifier. It is NOT
the `with(a#1, 1, with(a#1[1], 0, 42))` expression. The `with` expression
is the RHS of the separate SSA equation `a#2 = with(...)`.

When the bitvector encoder (`convert_index`) processes `a#2[1][0]`:

1. It sees `array = index_exprt(a#2, 1)`, `index = 0`
2. The inner `index_exprt(a#2, 1)` has `array = a#2`
3. It checks: is `a#2` a `with` expression? **No** — it's a symbol
4. The ITE encoding doesn't fire
5. Falls through to creating **free (unconstrained) bitvector variables**

At this point, the bitvector variables for `a#2[1][0]` are completely
unconstrained. The solver doesn't know they should equal 42.

## How Element-Wise Constraints Bridge the Gap

The array theory connects the symbol `a#2` to its definition `with(...)`
through element-wise constraints:

1. `record_array_equality` processes `a#2 = with(a#1, 1, with(a#1[1], 0, 42))`
   and unions them in the union-find

2. `add_array_constraints_with` generates for each index `k` in the index set:
   - Direct store: `a#2[1] = with(a#1[1], 0, 42)` (the stored inner array)
   - Else branch: `1 ≠ k → a#2[k] = a#1[k]` (unchanged rows)

3. These constraints connect the free variables for `a#2[1]` to the inner
   `with` expression, and transitively connect `a#2[1][0]` to `42`

Without element-wise constraints, the free variables remain unconstrained
and the assertion fails (the solver picks an arbitrary value).

## Why Back-End Flattening Can't Work

The 2D ITE encoding can walk a `with` chain when it sees the `with`
expression directly. For example, if the expression were:

```
index_exprt(
  index_exprt(with(a#1, 1, with(a#1[1], 0, 42)), 1),
  0)
```

the ITE encoding would see the `with`, match the 2D pattern, and generate
`ITE(1==1 && 0==0, 42, ...)` — correct.

But the SSA introduces the symbol `a#2` between the `with` and the read.
The ITE encoding sees `a#2` (symbol), not `with(...)` (definition). It
can't look through the symbol to its definition because:

- The definition is in a separate SSA equation processed earlier
- The bitvector encoder doesn't maintain a symbol-to-definition map
- The array theory is the component that bridges symbols to definitions

## The 77-91% Clause Reduction Potential

When we skip element-wise constraints for 2D stores (accepting the
correctness break), the clause counts drop dramatically:

| Benchmark | With element-wise | Without | Reduction |
|-----------|------------------|---------|-----------|
| 2D 4 stores | 6,711 | 1,547 | 77% |
| Multi_Dimensional_Array6 | 73,405 | 6,894 | 91% |

This shows the overhead of the nested array structure: element-wise
constraints at the outer level compare array-typed values, generating
constraints for every inner element at every outer index.

## The Solution: SSA-Level Flattening

To capture this reduction correctly, the flattening must happen BEFORE
SSA symbols are introduced. Instead of:

```
a#2 = with(a#1, 1, with(a#1[1], 0, 42))
x#2 = a#2[1][0]
```

the SSA should produce:

```
a_flat#2 = with(a_flat#1, 1*M+0, 42)
x#2 = a_flat#2[1*M+0]
```

Now `a_flat#2` is a single-level array. The read `a_flat#2[1*M+0]` is a
single-level access. The existing ITE encoding handles it:

1. `convert_index` sees `array = a_flat#2` (symbol)
2. Array theory connects `a_flat#2` to `with(a_flat#1, 1*M+0, 42)`
3. Element-wise: `1*M+0 ≠ k → a_flat#2[k] = a_flat#1[k]` (single level!)
4. Direct store: `a_flat#2[1*M+0] = 42`

The element-wise constraints are now single-level (comparing elements,
not inner arrays), which is much cheaper.

## Implementation Location

The flattening should happen in `goto-symex` during SSA construction,
specifically where array stores and reads are encoded:

- `a[i][j] = v` → `a_flat[i*M+j] = v` (linearize the store)
- `x = a[i][j]` → `x = a_flat[i*M+j]` (linearize the read)
- Array type: `array(array(T, M), N)` → `array(T, N*M)`

Counterexample traces would map back: `a_flat[k]` → `a[k/M][k%M]`.

The solver back-end would see only flat arrays and work optimally with
the existing ITE encoding, element-wise constraints, and WEG.
