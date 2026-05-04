# TypeScript Front-End for CBMC — Design and Implementation Plan

## 1. Goals

Verify TypeScript programs using CBMC's bounded model checking.
Support the TypeScript type system and ECMAScript runtime semantics.
Produce correct verification results — soundness over completeness.

## 2. Language References

### 2.1 Runtime Semantics: ECMAScript 2024 (ECMA-262)

Local copy: `~/ecma262/spec.html`
Online: https://tc39.es/ecma262/2024/

All runtime behavior (operator semantics, type coercion, control flow,
built-in methods) is defined by ECMA-262. We cite sections using the
format `ES2024 §sec-id: Title`.

Key sections for the TypeScript frontend:

**Types (ES2024 §6.1):**
- `sec-ecmascript-language-types-undefined-type`: The Undefined Type
- `sec-ecmascript-language-types-null-type`: The Null Type
- `sec-ecmascript-language-types-boolean-type`: The Boolean Type
- `sec-ecmascript-language-types-string-type`: The String Type
- `sec-ecmascript-language-types-number-type`: The Number Type
- `sec-object-type`: The Object Type

**Type Conversion (ES2024 §7.1):**
- `sec-toboolean`: ToBoolean
- `sec-tonumber-applied-to-the-string-type`: ToNumber Applied to String
- `sec-stringtonumber`: StringToNumber

**Operators (ES2024 §13):**
- `sec-addition-operator-plus`: The Addition Operator (+)
- `sec-subtraction-operator-minus`: The Subtraction Operator (-)
- `sec-typeof-operator`: The typeof Operator
- `sec-logical-not-operator`: Logical NOT Operator (!)
- `sec-relational-operators`: Relational Operators
- `sec-equality-operators`: Equality Operators
- `sec-isstrictlyequal`: IsStrictlyEqual (===)
- `sec-assignment-operators`: Assignment Operators
- `sec-conditional-operator`: Conditional Operator (?:)
- `sec-bitwise-shift-operators`: Bitwise Shift Operators
- `sec-binary-bitwise-operators`: Binary Bitwise Operators

**Statements (ES2024 §14):**
- `sec-let-and-const-declarations`: Let and Const Declarations
- `sec-variable-statement`: Variable Statement
- `sec-continue-statement`: The continue Statement
- `sec-break-statement`: The break Statement
- `sec-for-in-and-for-of-statements`: for-in, for-of, for-await-of

**Functions (ES2024 §15):**
- `sec-function-definitions`: Function Definitions
- `sec-arrow-function-definitions`: Arrow Function Definitions
- `sec-class-definitions`: Class Definitions
- `sec-async-function-definitions`: Async Function Definitions

**Built-in Objects (ES2024 §19-28):**
- `sec-math.*`: Math methods (abs, floor, ceil, sqrt, sin, cos, etc.)
- `sec-string.prototype.*`: String methods (charAt, indexOf, slice, etc.)
- `sec-array.prototype.*`: Array methods (push, pop, map, filter, etc.)
- `sec-json.parse`, `sec-json.stringify`: JSON operations
- `sec-promise-objects`: Promise Objects
- `sec-error-objects`: Error Objects

**Number Arithmetic (ES2024 §6.1.6.1):**
- `sec-numeric-types-number-remainder`: Number::remainder
- `sec-numeric-types-number-leftShift`: Number::leftShift
- `sec-numeric-types-number-bitwiseAND`: Number::bitwiseAND
- `sec-numeric-types-number-bitwiseOR`: Number::bitwiseOR
- `sec-numeric-types-number-bitwiseXOR`: Number::bitwiseXOR

### 2.2 Type System: TypeScript Handbook

Local copy: `~/TypeScript-Website/packages/documentation/copy/en/handbook-v2/`
Online: https://www.typescriptlang.org/docs/handbook/intro.html

The TypeScript type system is defined by the Handbook and the compiler
implementation. We cite Handbook sections using the format
`TSH: <filename>`.

Key sections:
- `TSH: Basics.md` — Type annotations, type inference
- `TSH: Everyday Types.md` — Primitives, arrays, unions, type aliases
- `TSH: Narrowing.md` — Type guards, control flow analysis
- `TSH: More on Functions.md` — Function types, overloads, generics
- `TSH: Object Types.md` — Interfaces, optional properties
- `TSH: Classes.md` — Class declarations, inheritance, access modifiers
- `TSH: Modules.md` — Import/export
- `TSH: Type Manipulation/Generics.md` — Generic types

Note: The TypeScript compiler is the ultimate authority for type system
behavior. When the Handbook is ambiguous, the compiler's behavior is
definitive.

### 2.3 Verification-Specific Semantics

Defined in this document. Not part of any language standard.

- `console.assert(cond)` → CBMC assertion property
- `nondet_number()` → CBMC nondet value (floatbv[64])
- `nondet_boolean()` → CBMC nondet value (bool)
- `nondet_string()` → CBMC nondet value (refined_string)
- `__CPROVER_assume(cond)` → CBMC assume statement
- `__CPROVER_assert(cond, msg)` → CBMC named assertion

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

| TypeScript | CBMC | ES2024 Reference |
|-----------|------|-----------------|
| `number` | `floatbv[64]` (IEEE 754 double) | `sec-ecmascript-language-types-number-type` |
| `boolean` | `bool` | `sec-ecmascript-language-types-boolean-type` |
| `string` | `refined_string_typet` | `sec-ecmascript-language-types-string-type` |
| `null` | sentinel value or pointer | `sec-ecmascript-language-types-null-type` |
| `undefined` | sentinel value | `sec-ecmascript-language-types-undefined-type` |
| `T[]` | `struct { length: int64, data: T[MAX] }` | `sec-array-initializer` |
| `interface/class` | `struct_typet` | `sec-object-type` |
| `T \| null` | tagged union or optional | TSH: Everyday Types.md |

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

ES2024 `sec-ecmascript-language-types-string-type`: "The String type is
the set of all ordered sequences of zero or more 16-bit unsigned integer
values ("elements") up to a maximum length of 2^53 - 1 elements."

Note: ECMAScript strings are UTF-16. CBMC's `refined_string_typet` uses
`unsignedbv{16}` for Java (UTF-16). We use the same for TypeScript.

### 3.4 Number Model: IEEE 754 Double

ES2024 `sec-ecmascript-language-types-number-type`: "The Number type has
exactly 18,437,736,874,454,810,627 values, representing the
double-precision 64-bit format IEEE 754-2019 values."

Use `floatbv[64]` (double_type()) for all `number` values.

For integer operations (bitwise, array indexing), typecast to
`signedbv[32]` per ES2024 `sec-numeric-types-number-bitwiseAND` which
specifies ToInt32 conversion.

### 3.5 Equality Semantics

ES2024 `sec-isstrictlyequal`: IsStrictlyEqual(x, y)
- TypeScript uses `===` (strict equality) by default
- No implicit type coercion (unlike `==`)
- For numbers: use `ieee_float_equal_exprt` (handles -0.0 === 0.0)
- For strings: use `cprover_string_equal_func`
- For booleans: use `equal_exprt`
- For objects: reference equality (pointer comparison)

ES2024 `sec-islooselyequal`: IsLooselyEqual(x, y)
- TypeScript discourages `==` but it's valid JavaScript
- Involves type coercion per the Abstract Equality Comparison algorithm
- Lower priority for implementation

### 3.6 Architecture

```
TypeScript source (.ts)
    ↓
ts_ast_to_json.js (Node.js + TypeScript Compiler API)
    ↓
JSON AST with types (_type field on every node)
    ↓
typescript_languaget::parse() (C++ — read JSON)
    ↓
typescript_convertert::convert() (C++ — JSON → GOTO)
    ↓
GOTO program
    ↓
CBMC verification (with --refine-strings)
```

### 3.7 Directory Layout

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
  assert-number/             — ES2024 §6.1.6.1 number arithmetic
  assert-boolean/            — ES2024 §6.1.1 boolean logic
  assert-string/             — ES2024 §6.1.4 string operations
  function-basic/            — ES2024 §15.2 function definitions
  if-else/                   — ES2024 §14.6 if statement
  while-loop/                — ES2024 §14.7.3 while statement
  array-basic/               — ES2024 §23.1 array objects
  interface-basic/           — TSH: Object Types.md
  nondet-basic/              — Verification primitives
  null-safety/               — TSH: Narrowing.md
```

## 4. Current Status

**115 CORE tests, 3 KNOWNBUG** (as of 2026-05-04, updated)

All phases 1-8 are substantially complete. Phase 9 is partially done.

### Implemented Features
- **Types**: number (IEEE 754 double), boolean, string (refined_string_typet),
  arrays (fixed-size struct), objects/interfaces (struct), classes (struct + methods),
  enums (numeric), generics (resolved by TS compiler)
- **Variables**: const/let, type annotations, destructuring (object + array)
- **Operators**: arithmetic, comparison (===, !==, <, >, <=, >=), logical (&&, ||, !),
  ternary, compound assignment (+=, -=, *=), postfix/prefix increment, nullish coalescing (??)
- **Control flow**: if/else, while, do-while, for, for-of, switch/case, try/catch, break
- **Functions**: declarations, arrow functions, generics, optional params with defaults,
  rest parameters (...args), nested functions with closure capture
- **Classes**: declarations, constructors, methods, this pointer, new expression,
  property access, inheritance (extends), super() calls
- **Arrays**: literals, indexing, push, pop, length, for-of, map, spread ([...a, ...b])
- **Strings**: refined_string_typet, literals, length, concatenation, template literals,
  methods (indexOf, includes, substring, toUpperCase, toLowerCase, trim, charAt,
  startsWith, endsWith) — all constant-evaluated at conversion time
- **Verification**: console.assert, nondet_number, __CPROVER_assume, console.log (no-op)
- **Math**: constant evaluation (sqrt, abs, floor, ceil, sin, cos, etc.) + symbolic Math.abs

### Remaining KNOWNBUGs
| Test | Issue |
|------|-------|
| higher-order-function | Function as parameter (callback types) |
| type-narrowing-typeof | Union types need tagged union model |

## 5. Implementation Phases

### Phase 1: Skeleton (target: 3 CORE tests)
- `typescript_languaget` class with parse/typecheck/generate_support
- `typescript_convertert` with basic expression/statement handling
- Number literals, `const`/`let` declarations, `console.assert`
- Register `.ts` file extension

### Phase 2: Scalar expressions (target: 5 CORE tests)
- Arithmetic: `+`, `-`, `*`, `/`, `%` (ES2024 §13.15)
- Comparison: `===`, `!==`, `<`, `>`, `<=`, `>=` (ES2024 §13.12-13.13)
- Logical: `&&`, `||`, `!` (ES2024 §13.14)
- Assignment: `=`, `+=`, `-=`, etc. (ES2024 §13.15)

### Phase 3: Control flow (target: 8 CORE tests)
- `if`/`else` (ES2024 §14.6)
- `while`, `for`, `for...of` (ES2024 §14.7)
- `break`, `continue` (ES2024 §14.8-14.9)
- `switch`/`case` (ES2024 §14.12)

### Phase 4: Functions (target: 10 CORE tests)
- Function declarations and calls (ES2024 §15.2)
- Parameters with types
- Return values (ES2024 §14.10)
- Arrow functions (ES2024 §15.3)

### Phase 5: Strings (target: 12 CORE tests)
- `refined_string_typet` integration with `--refine-strings`
- String literals, concatenation (ES2024 `sec-addition-operator-plus`)
- `length` (ES2024 `sec-string.prototype`), `charAt`, `indexOf`
- Template literals (ES2024 `sec-template-literals`)

### Phase 6: Arrays (target: 15 CORE tests)
- Array literals and indexing (ES2024 `sec-array-initializer`)
- `push`, `pop`, `length` (ES2024 `sec-array.prototype.*`)
- `for...of` iteration (ES2024 `sec-for-in-and-for-of-statements`)
- Bounds checking

### Phase 7: Objects and interfaces (target: 18 CORE tests)
- Interface definitions (TSH: Object Types.md)
- Object literals (ES2024 `sec-object-initializer`)
- Property access (ES2024 `sec-property-accessors`)
- Structural typing (TSH: Object Types.md)

### Phase 8: Classes (target: 20 CORE tests)
- Class declarations (ES2024 `sec-class-definitions`)
- Constructor, methods
- Inheritance (`extends`)
- `this` binding

### Phase 9: Advanced types (target: 22 CORE tests)
- Union types (TSH: Everyday Types.md)
- Type narrowing (TSH: Narrowing.md)
- Optional properties (TSH: Object Types.md)
- Generics (TSH: Type Manipulation/Generics.md)

## 5. Verification Primitives

```typescript
// Nondet values — return unconstrained symbolic values
declare function nondet_number(): number;
declare function nondet_boolean(): boolean;
declare function nondet_string(): string;

// Assumptions — constrain symbolic values
declare function __CPROVER_assume(cond: boolean): void;

// Assertions — verification properties
// Also: console.assert(cond) maps to CBMC assertion
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
   for conversion-time optimization.

6. **Test incrementally** — Run regression after every change.

7. **`clang-format-15` before every commit** — Enforced by CI.

8. **IEEE float equality** — Use `ieee_float_equal_exprt` for number
   comparisons per ES2024 `sec-isstrictlyequal`.

9. **Cross-reference everything** — Every implementation decision must
   cite the relevant ES2024 section or TSH page.
