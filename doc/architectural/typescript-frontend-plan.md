# TypeScript Front-End for CBMC — Design and Implementation Plan

## 1. Goals

Verify TypeScript programs using CBMC's bounded model checking.
Support the TypeScript type system and ECMAScript runtime semantics.
Produce correct verification results — soundness over completeness.

## 2. Language References

- **ECMAScript 2024 (ECMA-262)**: Runtime semantics for JavaScript
  https://tc39.es/ecma262/2024/
- **TypeScript Handbook**: Type system, narrowing, generics
  https://www.typescriptlang.org/docs/handbook/intro.html
- **TypeScript Compiler API**: Parsing and type checking
  https://github.com/microsoft/TypeScript/wiki/Using-the-Compiler-API

Note: The old TypeScript Language Specification (v1.8, 2016) is archived
and outdated. The Handbook + ECMAScript spec are authoritative.

## 3. Design Decisions

### 3.1 Parser: Use TypeScript Compiler API

Unlike Python (which uses `ast` module for parsing only), TypeScript's
compiler provides both parsing AND full type checking. We get:
- Complete AST with resolved types for every expression
- Type narrowing information (e.g., after `if (x !== null)`)
- Generic type instantiation
- Overload resolution

**Approach:** A Node.js script (`ts_ast_to_json.js`) invokes the
TypeScript Compiler API to produce a JSON AST with `_type` annotations
on every node. The C++ converter reads this JSON.

**Advantage over Python:** No type inference needed in the C++ converter.
TypeScript's type checker does all the work.

### 3.2 Type Strategy: Leverage TypeScript's Static Types

TypeScript has a complete static type system. Every variable, parameter,
and expression has a known type at compile time. This means:
- No tagged unions needed (unlike Python's `python_value_type`)
- No type inference in the converter
- Direct mapping from TS types to CBMC types

**Type mapping:**
| TypeScript | CBMC |
|-----------|------|
| `number` | `floatbv[64]` (IEEE 754 double) |
| `boolean` | `bool` |
| `string` | `refined_string_typet` (CBMC string solver) |
| `null` | `pointer_typet` (null pointer) |
| `undefined` | `signedbv[64]` (sentinel value) |
| `T[]` | `struct { length: int64, data: T[MAX] }` |
| `interface/class` | `struct_typet` |
| `T | null` | `struct { tag: int, value: T, is_null: bool }` |

### 3.3 String Model: Use `refined_string_typet` from Day 1

**Key learning from Python:** Fixed-size array strings (256 or 64 bytes)
are a performance bottleneck and precision limitation. CBMC has a
complete string solver (`refined_string_typet`, `cprover_string_*`
functions, `--refine-strings`) that the Java frontend uses.

For TypeScript, we use `refined_string_typet` from the start:
- Variable-length strings (no fixed limit)
- Efficient comparison via string solver (lazy refinement)
- Built-in support for concat, substring, indexOf, etc.
- Enable `--refine-strings` automatically for `.ts` files

### 3.4 Number Model: IEEE 754 Double

TypeScript's `number` is IEEE 754 double-precision floating-point.
Use `floatbv[64]` (double_type()) for all numbers.

For integer operations (bitwise, array indexing), typecast to
`signedbv[32]` or `signedbv[64]` as needed.

### 3.5 Architecture

```
TypeScript source (.ts)
    ↓
ts_ast_to_json.js (Node.js + TypeScript Compiler API)
    ↓
JSON AST with types
    ↓
typescript_languaget::parse() (C++ — read JSON)
    ↓
typescript_convertert::convert() (C++ — JSON → GOTO)
    ↓
GOTO program
    ↓
CBMC verification (with --refine-strings)
```

### 3.6 Directory Layout

```
src/typescript/
  typescript_language.h      — languaget subclass
  typescript_language.cpp    — parse, typecheck, generate_support
  typescript_converter.h     — converter class
  typescript_converter.cpp   — JSON AST → GOTO program
  typescript_types.h         — type helpers
  ts_ast_to_json.js          — Node.js AST converter
  CMakeLists.txt             — build configuration
  module_dependencies.txt    — CBMC module deps

regression/typescript/
  assert-number/             — basic number tests
  assert-boolean/            — boolean logic
  assert-string/             — string operations
  function-basic/            — function calls
  if-else/                   — control flow
  while-loop/                — loops
  array-basic/               — arrays
  interface-basic/           — interfaces/objects
  nondet-basic/              — verification primitives
  null-safety/               — null/undefined handling
```

## 4. Implementation Phases

### Phase 1: Skeleton (target: 3 CORE tests)
- `typescript_languaget` class with parse/typecheck/generate_support
- `typescript_convertert` with basic expression/statement handling
- Number literals, `const`/`let` declarations, `console.assert`
- Register `.ts` file extension

### Phase 2: Scalar expressions (target: 5 CORE tests)
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `===`, `!==`, `<`, `>`, `<=`, `>=`
- Logical: `&&`, `||`, `!`
- Assignment: `=`, `+=`, `-=`, etc.

### Phase 3: Control flow (target: 8 CORE tests)
- `if`/`else`
- `while`, `for`, `for...of`
- `break`, `continue`
- `switch`/`case`

### Phase 4: Functions (target: 10 CORE tests)
- Function declarations and calls
- Parameters with types
- Return values
- Arrow functions

### Phase 5: Strings (target: 12 CORE tests)
- `refined_string_typet` integration
- String literals, concatenation, comparison
- `length`, `charAt`, `indexOf`, `substring`
- Template literals

### Phase 6: Arrays (target: 15 CORE tests)
- Array literals and indexing
- `push`, `pop`, `length`
- `for...of` iteration
- Bounds checking

### Phase 7: Objects and interfaces (target: 18 CORE tests)
- Interface definitions
- Object literals
- Property access
- Structural typing

### Phase 8: Classes (target: 20 CORE tests)
- Class declarations
- Constructor, methods
- Inheritance (`extends`)
- `this` binding

### Phase 9: Advanced types (target: 22 CORE tests)
- Union types (`T | null`)
- Type narrowing
- Optional properties
- Generics (basic)

## 5. Verification Primitives

```typescript
// Nondet values
declare function nondet_number(): number;
declare function nondet_boolean(): boolean;
declare function nondet_string(): string;

// Assumptions
declare function __CPROVER_assume(cond: boolean): void;

// Assertions (also: console.assert)
declare function __CPROVER_assert(cond: boolean, msg: string): void;
```

## 6. Key Learnings from Python Frontend

1. **Use the language's own parser** — Don't write a custom parser.
   TypeScript's Compiler API gives us parsing + type checking for free.

2. **Start with KNOWNBUG tests** — Define goals before writing code.
   Move tests to CORE as features are implemented.

3. **Use `refined_string_typet`** — Don't repeat the fixed-size array
   mistake. Use CBMC's string solver from the start.

4. **Constant evaluation at conversion time** — For constant expressions,
   compute the result in C++ instead of generating solver constraints.

5. **Track constant values** — Maintain maps of known constant values
   (like `string_constants`, `dict_literals`) for conversion-time
   optimization.

6. **Test incrementally** — Run regression after every change. Commit
   frequently with descriptive messages.

7. **`clang-format-15` before every commit** — Enforced by CI.

8. **IEEE float equality** — Use `ieee_float_equal_exprt` for float
   comparisons (handles `-0.0 == 0.0`).
