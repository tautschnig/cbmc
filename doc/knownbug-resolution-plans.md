# KNOWNBUG Resolution Plans

## Classification

### String-solver dependent (defer):
1. **verify-string-reverse** — string array split("") + reverse + join("")
2. **verify-palindrome** — charAt with non-constant index
3. **verify-array-join** — string[] join with separator

### Non-string-solver (addressable now):
4. **verify-filter-map** — filter().map() chain
5. **verify-array-count** — filter().length chain
6. **verify-insertion-sort** — array element mutation via index assignment
7. **verify-closure-basic** — function returning closure
8. **verify-closure-counter** — function returning closure with mutable state
9. **verify-array-zip** — cross-array access (b.at(i)) in map callback
10. **verify-gcd-nondet** — solver performance with nondet + modulo loop
11. **verify-higher-order-predicate** — function pointer called in loop body
12. **verify-static-factory** — new expression inside static method return
13. **verify-spread-merge** — double spread with property override

---

## Detailed Plans (Non-String-Solver)

### 4. verify-filter-map — filter().map() chain

**Problem:** `nums.filter(pred).map(fn)` — the filter result is computed at
runtime via pending_stmts. The map handler can't resolve the filter result
symbol to a struct_exprt.

**Root cause:** Filter creates a result symbol and populates it via
conditional copies in pending_stmts. The symbol's value is never set to a
struct_exprt because the final contents depend on runtime predicate results.

**Fix plan:**
1. For CONSTANT arrays with INLINE predicates, evaluate the predicate at
   conversion time (constant folding).
2. In `convert_call_expression`, when the callee is a PropertyAccessExpression
   whose expression is ALSO a CallExpression (method chain), detect the
   filter→map pattern.
3. For each element of the source array, evaluate the filter predicate. If it
   passes (constant true), include it in the intermediate result.
4. Then apply the map function to the filtered elements.
5. Implementation: add a `try_constant_filter` helper that takes the source
   array struct and the predicate callback, evaluates each element, and returns
   a new struct_exprt with only the passing elements.

**Complexity:** Medium. Requires interpreting the callback body for constant
inputs.

---

### 5. verify-array-count — filter().length chain

**Problem:** `arr.filter(pred).length` — same as filter-map but accessing
`.length` on the filter result.

**Root cause:** Same as #4 — filter result is a runtime symbol without a
stored struct value.

**Fix plan:** Same as #4. Once constant filter evaluation works, the result
struct will have a correct length field. Alternatively:
1. After the filter handler creates its result, compute the length as a
   sum of predicate results (using if_exprt or conditional counting).
2. Store the length in the result symbol's struct.

**Complexity:** Medium. Shares solution with #4.

---

### 6. verify-insertion-sort — array element mutation via index

**Problem:** `result[j + 1] = result[j]` — assigning to array elements by
computed index.

**Root cause:** Our array model uses `struct{length, data[16]}`. Element
assignment via computed index requires `index_exprt` on the LHS of an
assignment, but the converter doesn't handle `arr[expr] = value` for
non-constant indices.

**Fix plan:**
1. In the expression statement handler, detect `ElementAccessExpression` on
   the LHS of an assignment.
2. Convert to: `code_frontend_assignt{index_exprt{member_exprt{arr, "data",
   arr_type}, idx_expr}, rhs}`
3. The LHS is `arr.data[idx]` which CBMC can handle natively.
4. Need to handle both constant and non-constant indices.
5. Also need to handle the case where `arr` is a local variable (symbol)
   vs a parameter.

**Complexity:** Low-Medium. The main challenge is that `result` is a copy
(from spread), so mutations need to target the copy's data member.

---

### 7. verify-closure-basic — function returning closure

**Problem:** `makeAdder(5)` returns `(x) => x + n` where `n` is captured.
Then `add5(3)` should call the returned function with the captured value.

**Root cause:** The converter handles closures (capture via extra params) but
doesn't support RETURNING a function from another function. The return value
is a function pointer, but the captured variable binding is lost.

**Fix plan:**
1. When a function returns an ArrowFunction/FunctionExpression, convert the
   inner function with captured variables (already works for nested calls).
2. The return value should be a `symbol_exprt` pointing to the inner function.
3. When the returned value is called (`add5(3)`), the call handler needs to:
   a. Resolve `add5` to the inner function symbol.
   b. Pass the captured values as extra arguments.
4. Implementation: store the captured variable VALUES at the call site of
   `makeAdder(5)` and bind them to the inner function.
5. Key insight: at the call site `makeAdder(5)`, we know `n = 5`. The inner
   function `__closure_0` has an extra param for `n`. When `add5` is called,
   we need to pass `5` as the extra arg.
6. Store a map: `variable_id → {function_id, captured_values[]}`.

**Complexity:** High. Requires tracking captured values across function
boundaries and binding them at call sites.

---

### 8. verify-closure-counter — closure with mutable state

**Problem:** Same as #7 but with mutable captured state (`count` is modified).

**Root cause:** Same as #7, plus the captured variable needs to be a
reference (pointer) rather than a value copy.

**Fix plan:** Depends on #7. Additionally:
1. Mutable captured variables need to be heap-allocated (or use a pointer).
2. The closure and the outer function share the same storage location.
3. Implementation: allocate captured mutable variables as global symbols
   and pass pointers to the closure.

**Complexity:** Very High. Requires reference semantics for captured vars.

---

### 9. verify-array-zip — cross-array access in map callback

**Problem:** `a.map((x, i) => x + b.at(i))` — the callback accesses array
`b` using the index `i` from the map iteration.

**Root cause:** The map handler converts the callback as a named function
and calls it for each element. But `b.at(i)` inside the callback can't
resolve `b` (it's an outer variable) or `i` (it's the index parameter).

**Fix plan:**
1. The callback already receives `(elem, idx)` as parameters.
2. The issue is that `b` is a free variable in the callback. The closure
   capture mechanism should handle this.
3. Check if `b` is being captured. If not, add it to the capture list.
4. When the map handler calls the callback for element `i`, pass `b` as
   an extra captured argument.
5. Inside the callback, `b.at(i)` should resolve: `b` is the captured
   array, `i` is the index parameter, `at(i)` does index access.

**Complexity:** Medium. The closure capture mechanism exists; need to ensure
it works for array-typed captured variables and that `at()` works on them.

---

### 10. verify-gcd-nondet — solver performance

**Problem:** GCD with nondet inputs and modulo in a while loop. The solver
times out (>10s).

**Root cause:** Float modulo (`%`) with nondet values creates very large
SAT formulas. The IEEE 754 encoding of `a % b` is expensive.

**Fix plan:**
1. Option A: Use `--object-bits` or solver tuning (not a frontend fix).
2. Option B: Model numbers as integers (signedbv) instead of floats when
   all operations are integer-like. Detect "integer mode" when values are
   constrained to integer ranges.
3. Option C: Increase timeout or mark as performance limitation.
4. Option D: Add `--ts-integer-mode` flag that uses signedbv[32] instead
   of floatbv[64] for number types.

**Complexity:** Option D is Medium (type system change). Options A/C are
trivial but don't fix the root cause.

---

### 11. verify-higher-order-predicate — function pointer in loop

**Problem:** `countWhere(data, isPositive)` — a function that takes an array
and a predicate, iterates with a for loop, and calls the predicate on each
element.

**Root cause:** The function `countWhere` receives `pred` as a parameter
(function pointer type). Inside the loop, `pred(arr[i])` is a call via
function pointer. CBMC needs to resolve the function pointer to the actual
function.

**Fix plan:**
1. The function pointer call `pred(arr[i])` needs to be converted to a
   `side_effect_expr_function_callt` with the function pointer as callee.
2. Check how the converter handles calls where the callee is an Identifier
   that resolves to a parameter of function type.
3. The issue: `pred` is a parameter with type `(x: number) => boolean`.
   When called as `pred(arr[i])`, the converter needs to emit a function
   pointer call.
4. CBMC's "removal of function pointers" pass should then resolve it to
   `isPositive` based on the call site.
5. Implementation: in the call handler, when the callee is an Identifier
   whose symbol has `pointer_typet{code_typet{...}}` type, emit a
   dereference + function call.

**Complexity:** Medium. Need to handle function-pointer-typed parameters
as callable.

---

### 12. verify-static-factory — new in static method return

**Problem:** `Pair.of(3, 7)` calls a static method that does `return new
Pair(a, b)`. The returned object's methods don't work.

**Root cause:** The static method creates a new object via `NewExpression`
and returns it. The `pending_stmts` from the constructor call need to be
flushed before the return value is used. The caller then calls `p.sum()`
on the returned object.

**Fix plan:**
1. The static method `of` creates `__new_Pair_N` and calls the constructor.
2. The return value is the symbol `__new_Pair_N`.
3. At the call site, `p = Pair.of(3, 7)` assigns the return value.
4. Then `p.sum()` should dispatch to `Pair::sum` with `&p` as this.
5. Check if the issue is that the constructor's pending_stmts aren't
   being included in the static method's body.
6. Fix: ensure `convert_expression` for NewExpression flushes pending_stmts
   into the function body when inside a function declaration.

**Complexity:** Medium. The pending_stmts mechanism needs to work correctly
inside static method bodies.

---

### 13. verify-spread-merge — double spread override

**Problem:** `{ ...a, ...b }` where `b` has a property `y` that should
override `a.y`.

**Root cause:** The spread handler processes spreads left-to-right but
doesn't override earlier properties. It builds the struct by concatenating
all fields from both objects.

**Fix plan:**
1. In the object literal handler, when processing multiple spreads:
   a. Collect all properties from all sources in order.
   b. For duplicate property names, use the LAST value (rightmost wins).
2. Implementation: build a map of `property_name → value`, processing
   spreads left-to-right. Later values override earlier ones.
3. Then construct the struct_exprt from the final map.

**Complexity:** Low. Just need to deduplicate properties in the spread
handler, keeping the last value for each name.

---

## Priority Order (for implementation)

1. **verify-spread-merge** (Low complexity, simple fix)
2. **verify-insertion-sort** (Low-Medium, enables array algorithms)
3. **verify-higher-order-predicate** (Medium, enables HOF patterns)
4. **verify-array-zip** (Medium, depends on closure capture for arrays)
5. **verify-static-factory** (Medium, pending_stmts in functions)
6. **verify-filter-map + verify-array-count** (Medium, constant filter eval)
7. **verify-closure-basic** (High, function-as-return-value)
8. **verify-closure-counter** (Very High, mutable closure state)
9. **verify-gcd-nondet** (Medium but architectural — integer mode)
