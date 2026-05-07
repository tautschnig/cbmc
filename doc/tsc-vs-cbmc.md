# TypeScript Compiler (tsc) vs CBMC

Both `tsc` (the TypeScript compiler) and CBMC perform static analysis on
TypeScript code. This document explains what each tool does, how they
differ, and when to use each one (or both together).

## Quick Summary

| Aspect | tsc | CBMC |
|--------|-----|------|
| Primary purpose | Type checking, transpilation | Verification of runtime behavior |
| What it checks | Type correctness (compile-time) | Runtime properties (bounded) |
| What it reports | Type errors, warnings | Assertion violations, counterexamples |
| Output | JavaScript + type info | Verification result (OK/FAILED + trace) |
| Completeness | Type-level (incomplete for runtime bugs) | Bounded (within depth) |
| Speed | Fast (seconds per file) | Slow (seconds per test) |

**Use tsc** for: type checking, JavaScript generation, IDE support, basic correctness

**Use CBMC** for: assertion verification, reachability, safety properties,
finding specific bugs

**Use both**: tsc catches most issues early; CBMC verifies deeper properties.

## What tsc Checks

tsc is a type checker — it verifies that types match declarations at
compile time. It catches:

- **Type mismatches**: `const x: number = "hello"` → error
- **Null/undefined access**: `const x: number | undefined = ...; x.toFixed()` → error
- **Missing properties**: `const obj: { x: number } = { y: 1 }` → error
- **Wrong argument types**: `function f(x: number); f("hello")` → error
- **Private access**: `obj.#priv` from outside the class → error
- **Immutable violations**: `const x: readonly number[]; x[0] = 5` → error

tsc does NOT check:

- **Assertion violations**: `assert(x > 0)` with unknown `x`
- **Array bounds**: `arr[i]` where `i` is out of range
- **Division by zero**: `a / b` where `b` might be 0
- **Integer overflow**: `2000000000 + 2000000000` wraps silently
- **Invariant preservation**: e.g., "balance is always >= 0"
- **Reachability**: "is this branch ever taken?"
- **Control flow properties**: "does this loop terminate?"

## What CBMC Checks

CBMC performs **bounded model checking**:

- Unfolds loops up to a specified bound (`--unwind N`)
- Treats `console.assert(cond)` and `__CPROVER_assert(cond)` as properties
- Explores all possible executions up to the bound
- For each reachable assertion, checks if it can be violated
- Produces **counterexamples** (concrete inputs that trigger the bug)

CBMC checks (when enabled):

- **Assertion violations**: any `console.assert(x)` failing
- **Array bounds**: `--bounds-check` flag
- **Division by zero**: `--float-div-by-zero-check`
- **NaN generation**: `--nan-check`
- **Private field access**: enforced by our frontend (beyond tsc)
- **Contract invariants**: `__CPROVER_loop_invariant`, `_requires`, `_ensures`

CBMC does NOT check (unlike tsc):

- Type correctness at compile time
- Runtime type of unknown values (dynamic dispatch)
- Infinite behaviors beyond the unwinding bound

## Complementary Workflow

Typical verification flow:
1. Write TypeScript with tsc type checking (IDE + CI)
2. Use CBMC to verify critical properties:
   ```typescript
   function transfer(from: Account, to: Account, amt: number): void {
     console.assert(from.balance >= amt);  // CBMC verifies
     from.balance -= amt;
     to.balance += amt;
     console.assert(from.balance + to.balance === before);  // invariant
   }
   ```
3. tsc catches type errors; CBMC catches logical bugs.

## What Our Frontend Adds Over tsc

Our CBMC TypeScript frontend (`cbmc foo.ts`) provides features beyond tsc:

### Runtime verification
- Array bounds (when `--bounds-check`)
- Integer overflow (when `--ts-integer-mode`)
- Division by zero (when `--float-div-by-zero-check`)
- NaN propagation (when `--nan-check`)

### Verification primitives
- `__CPROVER_assume(cond)`: constrain nondet inputs
- `__CPROVER_assert(cond, msg)`: custom checks
- `__CPROVER_loop_invariant(cond)`: loop invariants
- `__CPROVER_requires(cond)`: preconditions
- `__CPROVER_ensures(cond)`: postconditions
- `nondet_number()`, `nondet_string()`, etc.: model any input

### Counterexamples
When a property fails, CBMC produces a trace showing exactly which
inputs led to the failure — often more informative than a stack trace.

### Private field enforcement
tsc checks private access at compile time; our frontend also checks
it at verification time (if user bypasses tsc).

## What tsc Does That We Don't

- **Full structural typing**: we approximate (some type widenings are lost)
- **Strict null checks**: we don't track undefined separately from the value type
- **Index signatures**: `{ [key: string]: number }` partially supported
- **Mapped types**: `Record<K, V>` works via type checker resolution; more complex mapped types don't
- **Conditional types**: `T extends U ? X : Y` — not resolved
- **Infer types**: `infer T` — not supported
- **Module resolution for node_modules**: we use the TypeScript checker,
  which handles standard resolution

## Practical Example

```typescript
function divideBy(a: number, b: number): number {
  return a / b;  // tsc: OK (both are number)
}

console.assert(divideBy(10, 2) === 5);
console.assert(divideBy(10, 0) !== Infinity);  // tsc: OK
```

- **tsc**: passes (types are correct)
- **CBMC** (without flags): both assertions verified for concrete values
- **CBMC** `--float-div-by-zero-check`: catches division by 0 in `divideBy(10, 0)`
- **CBMC** `--nan-check`: catches potential NaN from 0/0

## Summary

| Check | tsc | CBMC (default) | CBMC (with flags) |
|-------|-----|-----|---|
| Type correctness | ✓ | ✗ | ✗ |
| Assertion violations | ✗ | ✓ | ✓ |
| Array bounds | ✗ | ✗ | ✓ (`--bounds-check`) |
| Division by zero | ✗ | ✗ | ✓ (`--float-div-by-zero-check`) |
| Integer overflow | ✗ | ✗ | ✓ (`--ts-integer-mode`) |
| NaN generation | ✗ | ✗ | ✓ (`--nan-check`) |
| Private access | ✓ (compile) | ✓ (runtime) | ✓ (runtime) |
| Counterexamples | ✗ | ✓ | ✓ |
| Loop termination | ✗ | With `--unwinding-assertions` | ✓ |
| Invariants | ✗ | ✓ (via primitives) | ✓ |

**Bottom line**: tsc is fast type-correctness checking. CBMC is slower
but checks deeper properties with explicit verification goals. Use
both — tsc for your IDE and CI baseline, CBMC for critical code paths.
