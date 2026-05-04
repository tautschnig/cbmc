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

- **Union types** (`number | string`) are not yet supported as tagged unions.
  The `typeof` narrowing pattern does not work.
- **Closures** capture variables from the immediately enclosing function only.
  Multi-level closure chains are not supported.
- **String operations** are evaluated at conversion time for constant strings.
  Symbolic string reasoning (e.g., nondet strings) requires `--refine-strings`.
- **Module imports** (`import`/`export`) are not supported. All code must be
  in a single file.
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

## Architecture

See `doc/architectural/typescript-frontend-plan.md` for the full design
document, including implementation phases and cross-references to the
ECMAScript 2024 specification and TypeScript Handbook.
