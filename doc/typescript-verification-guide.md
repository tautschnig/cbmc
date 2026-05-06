# TypeScript Verification Guide

CBMC includes an experimental TypeScript front-end that verifies TypeScript
programs using bounded model checking. It uses the TypeScript Compiler API
for parsing and type-checking, then converts the typed AST to GOTO programs
for verification.

## Requirements

- Node.js (v16+) with `typescript` package installed globally or in the
  project directory
- CBMC built with TypeScript support (the `typescript` library in `src/`)

## Quick Start

```typescript
// example.ts
const x: number = nondet_number();
__CPROVER_assume(x >= 0 && x <= 100);
const y: number = x * 2;
console.assert(y <= 200);
```

```bash
cbmc example.ts
```

## Verification Primitives

### Nondeterministic Values

```typescript
declare function nondet_number(): number;
declare function nondet_boolean(): boolean;
```

These return unconstrained symbolic values that CBMC explores all
possibilities for.

### Assumptions

```typescript
__CPROVER_assume(condition);
```

Constrains the symbolic execution to paths where `condition` holds.

### Assertions

```typescript
console.assert(condition);
console.assert(condition, "optional message");
```

CBMC checks that `condition` holds on all reachable paths. The optional
message is currently ignored but documents the intent.

## Supported Language Features

### Types
- `number` — IEEE 754 double-precision float
- `boolean` — true/false
- `string` — CBMC refined string type (constant evaluation at conversion time)
- Arrays (`number[]`, `string[]`, etc.) — fixed-size struct model
- Objects and interfaces — struct model
- Classes — struct with methods
- Enums — numeric values
- Generics — resolved by the TypeScript compiler before conversion

### Variables and Operators
- `const` and `let` declarations with type annotations
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `===`, `!==`, `<`, `>`, `<=`, `>=`
- Logical: `&&`, `||`, `!`
- Compound assignment: `+=`, `-=`, `*=`, etc.
- Increment/decrement: `++`, `--`
- Ternary: `condition ? a : b`
- Nullish coalescing: `??`
- Destructuring: `const { x, y } = obj` and `const [a, b] = arr`

### Control Flow
- `if`/`else`
- `while`, `do-while`, `for`, `for...of`
- `switch`/`case`/`default`
- `break`
- `try`/`catch` (simplified model)

### Functions
- Function declarations and arrow functions
- Optional parameters with defaults
- Rest parameters (`...args`)
- Nested functions with closure capture
- Generic functions (resolved by TypeScript compiler)

### Classes
- Property declarations and constructors
- Methods with `this` binding
- Inheritance (`extends`) and `super()` calls
- Multiple instances with independent state

### Arrays
- Literals, indexing, `length`
- `push`, `pop`
- `map(callback)`, `filter(predicate)`
- Spread: `[...a, ...b]`
- `for...of` iteration

### Strings
- Literals and template literals
- `length` property
- Methods: `indexOf`, `includes`, `substring`, `toUpperCase`, `toLowerCase`,
  `trim`, `charAt`, `startsWith`, `endsWith`
- Concatenation with `+`
- All string operations are constant-evaluated at conversion time

### Math
- `Math.abs`, `Math.floor`, `Math.ceil`, `Math.sqrt`, `Math.round`
- `Math.sin`, `Math.cos`, `Math.tan`, `Math.log`, `Math.exp`
- `Math.min`, `Math.max`, `Math.pow`
- Constant evaluation for known values; symbolic `Math.abs` for nondet

## Limitations

- **Union types** (`number | string`) are supported with typeof narrowing.
- **Closures** capture variables from the immediately enclosing function.
- **String operations** are evaluated at conversion time for constant strings.
  Symbolic string reasoning (e.g., nondet strings) requires `--refine-strings`.
- **Module imports** are supported for relative imports (`./module`).
  Third-party packages (node_modules) are not supported.
- **Async/await**, Promises, generators, and iterators are not supported.
- **Map**, **Set**, and other built-in collection types are not modeled.
- **Regular expressions** are not supported.
- **DOM APIs** and Node.js APIs are not available.

## Loop Unwinding

For programs with loops, use `--unwind N` to set the maximum loop iterations:

```bash
cbmc --unwind 10 --no-unwinding-assertions example.ts
```

Without `--no-unwinding-assertions`, CBMC will report a failure if any loop
could execute more than N iterations.

## Example: Verifying a Binary Search

```typescript
function binarySearch(arr: number[], target: number): number {
  let lo: number = 0;
  let hi: number = arr.length - 1;
  while (lo <= hi) {
    const mid: number = lo + Math.floor((hi - lo) / 2);
    if (arr[mid] === target) return mid;
    if (arr[mid] < target) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

const sorted: number[] = [1, 3, 5, 7, 9];
console.assert(binarySearch(sorted, 5) === 2);
console.assert(binarySearch(sorted, 4) === -1);
```

```bash
cbmc --unwind 10 --no-unwinding-assertions search.ts
```

## Multi-File Projects

CBMC supports multi-file TypeScript projects with `import`/`export`:

```typescript
// math.ts
export function add(a: number, b: number): number { return a + b; }

// main.ts
import { add } from './math';
console.assert(add(2, 3) === 5);
```

```bash
cbmc main.ts  # automatically resolves and includes imported files
```

The TypeScript compiler resolves imports automatically. Supported:
- Named imports: `import { foo } from './module'`
- Interface/class imports: `import { MyClass } from './module'`
- Enum imports: `import { Color } from './module'`

Not yet supported:
- `node_modules` imports (third-party packages)
- Dynamic imports (`import()`)
- Default exports

## Verification Flags

```bash
# Array bounds checking (on by default)
cbmc --bounds-check example.ts

# Division by zero checking
cbmc --float-div-by-zero-check example.ts

# Disable bounds checking
cbmc --no-bounds-check example.ts
```

## Additional Verification Primitives

```typescript
// Make variable nondeterministic
__CPROVER_havoc_object(x);

// Coverage goal
__CPROVER_cover(condition);

// Direct assertion (alternative to console.assert)
__CPROVER_assert(condition);

// Nondeterministic array
declare function nondet_array(): number[];
```

## Architecture

See `doc/architectural/typescript-frontend-plan.md` for the full design
document, and `doc/architectural/typescript-multifile-plan.md` for the
multi-file support architecture.

---

## Advanced Features (v500)

### Integer inference mode

By default, all `number` types use IEEE 754 double-precision floats. For
programs that use integer-only arithmetic (counters, indices, modulo),
enable optimization with `--ts-integer-mode`. This can be orders of
magnitude faster for integer-heavy programs:

```
cbmc --ts-integer-mode --unwind 15 --no-unwinding-assertions program.ts
```

The inference uses `signedbv[64]` for variables used with integer
operators (`%`, `&`, `|`, `^`, `<<`, `>>`). Variables used with `*` or
`/` remain `floatbv[64]` to avoid overflow or fraction issues.

### Generics

Both generic functions and generic classes are supported via
monomorphization:

```typescript
function identity<T>(x: T): T { return x; }
const a: number = identity<number>(42);      // calls identity__number
const b: string = identity<string>("hi");    // calls identity__string

class Box<T> {
  value: T;
  constructor(v: T) { this.value = v; }
}
const b1 = new Box<number>(42);   // creates Box__number
const b2 = new Box<string>("hi"); // creates Box__string
```

### Map and Set

`Map<K, V>` and `Set<T>` are modeled with bounded storage (8 entries).
Operations: `set`/`add`, `get`, `has`, `delete`, `size`. `for...of`
iteration works.

```typescript
const m = new Map<string, number>();
m.set("a", 1);
for (const [key, value] of m) { /* ... */ }
```

### Private field access control

Private fields (`private x: number` or `#x: number`) are enforced at
verification time — accessing a private field from outside the class
triggers an assertion failure:

```typescript
class Account {
  private balance: number;
  constructor(b: number) { this.balance = b; }
}
const a = new Account(100);
a.balance; // CBMC reports: access to private field 'balance'
```

### Verification primitives

- `__CPROVER_assume(cond)` — constrain nondet values
- `__CPROVER_assert(cond, msg)` — custom assertion
- `__CPROVER_loop_invariant(cond)` — loop invariant
- `__CPROVER_requires(cond)` — function precondition
- `__CPROVER_ensures(cond)` — function postcondition
- `__CPROVER_cover(cond)` — coverage goal

### Verification flags

- `--bounds-check` — array bounds verification
- `--float-div-by-zero-check` — division by zero
- `--nan-check` — NaN detection
- `--ts-integer-mode` — integer type inference
- `--ts-max-array-size N` — array size (default 16)
- `--refine-strings` — string solver (optional)

### String solver (--refine-strings) compatibility

CBMC's `--refine-strings` flag enables the string solver, which provides
axioms for CBMC's native `refined_string_typet`. Our TypeScript frontend
uses a different string model (inline array with fixed bound), so
`--refine-strings` is **not required** for our string operations.

All 51 regression tests in the string/integration test categories pass
both with and without the flag. The flag is compatible but provides no
additional capability for our model.

The URL parser integration test (`integration-url-parser`) hits CBMC
solver limitations (`boolbv_width::get_entry`) regardless of whether
`--refine-strings` is enabled. This is due to the size of the formula
generated by nested symbolic string operations, not the string model.
