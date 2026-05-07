# TypeScript Frontend Capability Matrix

Status of each ES2024 / TypeScript Handbook feature in the CBMC TypeScript
frontend as of 2026-05-07. Compiled from systematic code review and
probe-testing against the specification.

**Legend:**
- ✅ Supported — feature works correctly
- ⚠️  Partial — feature works in common cases but has documented gaps
- ❌ Not supported — feature returns wrong result or nondet
- ⏳ Limited — feature works but requires workarounds (e.g., `--unwind`,
  `--ts-integer-mode`, `--no-unwinding-assertions`)

## Language fundamentals

| Feature | Status | Notes |
|---------|--------|-------|
| Numeric literals (int, float, hex, bin, oct, BigInt) | ⚠️ | BigInt not modeled; numeric literals converted to IEEE-754 double |
| String literals (single, double, template) | ✅ | Template literals with interpolation work |
| Boolean literals | ✅ | |
| `undefined`, `null` | ⚠️ | `undefined` union-field works; nullish operators (`??`, `?.`) limited |
| Variable declarations (`let`, `const`, `var`) | ✅ | |
| Arithmetic operators (`+`, `-`, `*`, `/`, `%`, `**`) | ✅ | |
| Comparison operators (`<`, `<=`, `>`, `>=`, `==`, `===`, `!=`, `!==`) | ✅ | NaN handling per IEEE 754 |
| Logical operators (`&&`, `\|\|`, `!`) | ✅ | |
| Bitwise operators (`\|`, `&`, `^`, `~`, `<<`, `>>`, `>>>`) | ✅ | Int32 conversion semantics |
| Assignment operators (`=`, `+=`, etc.) | ✅ | |
| `typeof` operator (ES2024 §12.5.5) | ✅ | Now handles literal types ("42" → "number"); fixed 2026-05-07 |
| `instanceof` operator | ✅ | |
| `in` operator | ✅ | Including narrowing |
| Comma operator | ✅ | |
| Conditional operator (`? :`) | ✅ | |
| Nullish coalescing (`??`) (ES2024 §13.13) | ❌ | Known bug: returns LHS instead of RHS when LHS is undefined (`nullish-coalescing` KNOWNBUG) |
| Optional chaining (`?.`) (ES2024 §13.3.9) | ❌ | Known bug: doesn't short-circuit (`optional-chaining` KNOWNBUG) |
| String-to-number coercion (`+`, `Number()`) | ❌ | `+"42"` doesn't parse |
| Number-to-string coercion (in `+`) | ✅ | Fixed 2026-05-07: `"x" + 1 === "x1"` |
| Boolean-to-string coercion (in `+`) | ✅ | Fixed 2026-05-07: `"x" + true === "xtrue"` |

## Control flow

| Feature | Status | Notes |
|---------|--------|-------|
| `if`/`else` | ✅ | |
| `switch`/`case`/`default` | ✅ | |
| `for` (classic) | ⏳ | Needs `--no-unwinding-assertions` or `--ts-integer-mode` due to float loop counter |
| `for..of` | ✅ | |
| `for..in` | ⚠️ | Array keys iteration limited |
| `while`, `do..while` | ✅ | Same loop-counter caveat as `for` |
| `break`, `continue` | ✅ | Including labeled |
| `return` | ✅ | |
| `throw` | ✅ | |
| `try`/`catch`/`finally` | ✅ | |

## Functions

| Feature | Status | Notes |
|---------|--------|-------|
| Function declarations | ✅ | |
| Function expressions | ✅ | |
| Arrow functions | ✅ | |
| Default parameters | ✅ | |
| Rest parameters (`...args`) | ✅ | |
| Spread in calls (`f(...arr)`) | ✅ | |
| Overloads | ⚠️ | Declaration syntax accepted, one implementation variant used |
| Closures | ✅ | Including multi-closure mutable capture |
| Recursion | ✅ | |
| IIFE | ✅ | |
| `this` binding | ⚠️ | Method calls work; explicit rebinding (`call`/`apply`/`bind`) limited |

## Classes (TSH: Classes)

| Feature | Status | Notes |
|---------|--------|-------|
| Class declarations | ✅ | |
| Constructors | ✅ | Including parameter property syntax |
| Instance methods | ✅ | |
| Static methods | ✅ | |
| Getters / setters | ✅ | |
| Private fields (`#name`) | ✅ | Enforced at type-check |
| Inheritance (`extends`) | ✅ | |
| `super` calls | ✅ | |
| `instanceof` | ✅ | |
| Abstract classes | ⚠️ | Method signatures accepted, abstract-ness not enforced |
| Polymorphic dispatch | ✅ | Via stored class tag |

## Generics (TSH: Generics)

| Feature | Status | Notes |
|---------|--------|-------|
| Generic functions (`<T>`) | ✅ | Via monomorphization |
| Generic classes (`class<T>`) | ✅ | Via monomorphization |
| Constraints (`T extends X`) | ✅ | |
| Default type parameters | ⚠️ | Simple defaults work |
| Heterogeneous tuples (`<A, B>(..): [A, B]`) | ❌ | Type unification fails (`generic-heterogeneous-tuple` KNOWNBUG) |
| Utility types (`Pick`, `Omit`, `Readonly`, etc.) | ✅ | Via TS compiler resolution |

## Type system

| Feature | Status | Notes |
|---------|--------|-------|
| Interfaces | ✅ | |
| Type aliases | ✅ | |
| Union types (`A \| B`) | ✅ | Tagged union struct model |
| Intersection types (`A & B`) | ⚠️ | Simple cases merged; complex unions may lose info |
| Discriminated unions | ✅ | Merged struct model; narrowing via discriminant field |
| Literal types (string, number, boolean) | ✅ | |
| Template literal types | ✅ | Simple patterns |
| Optional properties | ✅ | |
| Readonly properties | ✅ | Stripped for symbolic reasoning |
| Index signatures (`[k: string]: T`) | ⚠️ | Basic get/set works, iteration limited |
| Conditional types | ⚠️ | Simple cases work; nested inference limited |
| Mapped types | ⚠️ | Utility types via TSC; custom mapped types limited |

## Narrowing (TSH: Narrowing)

| Feature | Status | Notes |
|---------|--------|-------|
| `typeof` narrowing | ✅ | Fixed for literal types |
| `instanceof` narrowing | ✅ | |
| `in` operator narrowing | ✅ | |
| Discriminated union narrowing | ✅ | |
| User-defined type guards | ⚠️ | Non-union parameters supported; union refinement limited |
| Truthiness narrowing | ⚠️ | |

## Enums (TSH: Enums)

| Feature | Status | Notes |
|---------|--------|-------|
| Numeric enums | ✅ | Auto-numbering from 0 |
| Numeric enums with values | ✅ | |
| Const enums | ✅ | |
| String enums | ❌ | Values not tracked through access (`enum-string-values` KNOWNBUG) |
| Heterogeneous enums | ⚠️ | |

## Destructuring

| Feature | Status | Notes |
|---------|--------|-------|
| Array destructuring | ✅ | |
| Array destructuring with rest | ✅ | |
| Array destructuring with defaults | ⚠️ | |
| Object destructuring (plain) | ✅ | |
| Object destructuring with rename | ✅ | Fixed 2026-05-07 |
| Object destructuring with defaults | ⚠️ | |
| Nested destructuring | ⚠️ | |

## Built-in types

### Array (ES2024 §23.1)

| Method | Status | Notes |
|--------|--------|-------|
| `length` | ✅ | |
| `push`, `pop` (mutation) | ⚠️ | pop() mutates correctly but return value not tracked (`array-pop-return` KNOWNBUG) |
| `shift`, `unshift` | ✅ | |
| `map`, `filter`, `reduce` | ✅ | |
| `forEach` | ✅ | |
| `find`, `findIndex` | ✅ | |
| `includes`, `indexOf` | ✅ | |
| `every`, `some` | ✅ | |
| `slice`, `splice` | ✅ | |
| `concat` | ✅ | |
| `flat`, `flatMap` | ✅ | Single-level flat |
| `join`, `split` | ✅ | |
| `reverse`, `fill` | ✅ | |
| `at` | ✅ | |
| `sort` (no comparator) | ⚠️ | |
| `sort` (with comparator) | ❌ | Not implemented (`array-sort-comparator` KNOWNBUG) |
| `Array.from` | ✅ | |
| `Array.isArray` | ✅ | Fixed 2026-05-07 |
| `Array.of` | ⚠️ | |
| Spread `[...arr]` | ✅ | |
| Destructuring `[a, b]` | ✅ | |

### String (ES2024 §22.1)

| Feature | Status | Notes |
|---------|--------|-------|
| `length` | ✅ | |
| `charAt` | ✅ | |
| `charCodeAt` | ❌ | Not implemented |
| `String.fromCharCode` | ❌ | Not implemented |
| `indexOf`, `lastIndexOf` | ✅ | Non-constant via symbolic scan (depth-limited) |
| `includes`, `startsWith`, `endsWith` | ✅ | |
| `substring`, `slice`, `substr` | ✅ | |
| `split`, `replace`, `replaceAll` | ✅ | |
| `toLowerCase`, `toUpperCase` | ✅ | |
| `trim`, `trimStart`, `trimEnd` | ✅ | |
| `padStart`, `padEnd`, `repeat` | ✅ | |
| Template literals with interpolation | ✅ | |
| Unicode (BMP) | ⚠️ | Char codes use UTF-16; complex Unicode handling limited |
| Surrogate pairs (emoji) | ⚠️ | |
| String index (`s[0]`) | ⚠️ | Returns char code (number), spec says string; divergence |

### Map (ES2024 §24.1)

| Method | Status | Notes |
|--------|--------|-------|
| `set`, `get`, `has`, `delete` | ✅ | |
| `size` | ✅ | |
| `clear` | ✅ | |
| `forEach` | ⚠️ | |
| Iteration (`for..of`, `keys`, `values`, `entries`) | ⚠️ | |

### Set (ES2024 §24.2)

| Method | Status | Notes |
|--------|--------|-------|
| `add`, `delete` | ✅ | |
| `size` | ✅ | |
| `has` | ❌ | Not tracking membership (`set-has` KNOWNBUG) |
| `clear` | ✅ | |
| Iteration | ⚠️ | |

### Math (ES2024 §21.3)

| Method | Status | Notes |
|--------|--------|-------|
| `abs`, `floor`, `ceil`, `round`, `trunc` | ✅ | |
| `sqrt`, `pow`, `exp`, `log` | ✅ | |
| `sin`, `cos`, `tan` (trig) | ✅ | |
| `max`, `min` | ✅ | Variadic, fixed 2026-05-07 |
| `random` | ⚠️ | Returns nondet in [0, 1) |
| `PI`, `E`, etc. (constants) | ✅ | |
| `Math.hypot`, `Math.sign`, `Math.cbrt` | ⚠️ | |

### Number (ES2024 §21.1)

| Method | Status | Notes |
|--------|--------|-------|
| `Number.isInteger`, `Number.isNaN`, `Number.isFinite` | ✅ | |
| `Number.MAX_SAFE_INTEGER`, `MIN_SAFE_INTEGER` | ✅ | |
| `Number.EPSILON`, `MAX_VALUE`, `MIN_VALUE` | ✅ | |
| `toFixed`, `toString` | ⚠️ | Constant case handled; symbolic case limited |

### Error (ES2024 §20.5)

| Feature | Status | Notes |
|---------|--------|-------|
| `throw new Error(msg)` | ✅ | |
| `try`/`catch` with Error | ✅ | |
| Subclasses (`TypeError`, etc.) | ⚠️ | |

### Promise (ES2024 §27.2)

| Feature | Status | Notes |
|---------|--------|-------|
| `Promise.resolve(x)` | ✅ | Treated as x synchronously |
| `Promise.reject(e)` | ⚠️ | |
| `.then(fn)` | ✅ | |
| `.catch(fn)` | ✅ | |
| `.finally(fn)` | ✅ | |
| `Promise.all([...])` | ✅ | Sequential evaluation |
| `Promise.race`, `Promise.any` | ⚠️ | |
| `async`/`await` (sequential) | ✅ | Default |
| `async`/`await` (threading) | ⏳ | Via `--ts-async-threading` opt-in |

## Modules (TSH: Modules)

| Feature | Status | Notes |
|---------|--------|-------|
| `import { x } from "mod"` | ✅ | |
| `import * as ns from "mod"` | ✅ | |
| `import x from "mod"` (default) | ✅ | |
| `export` (named, default) | ✅ | |
| `export * from "mod"` (re-export) | ⚠️ | |
| Dynamic `import()` | ❌ | Not implemented |

## CBMC-specific extensions

| Feature | Status | Notes |
|---------|--------|-------|
| `__CPROVER_assume` | ✅ | |
| `__CPROVER_loop_invariant` | ✅ | |
| `__CPROVER_requires`, `_ensures` | ✅ | |
| `nondet_number()`, `nondet_boolean()`, `nondet_string()` | ✅ | |
| `--ts-integer-mode` (opt-in) | ✅ | Use int64 instead of float64 for numbers |
| `--ts-async-threading` (opt-in) | ✅ | Async interleaving via CBMC threads |
| `--ts-max-array-size` | ✅ | |
| `--nan-check` | ✅ | |

## Frontend behavior

| Area | Status | Notes |
|------|--------|-------|
| `.ts` extension recognized | ✅ | |
| `.tsx` extension recognized | ✅ | |
| Parser via TypeScript Compiler API | ✅ | Node.js subprocess |
| Type checker integration (utility types) | ✅ | |
| Multi-file projects | ✅ | |
| `tsconfig.json` | ⚠️ | Defaults used; custom config not read |
| Source location in traces | ✅ | |

## Known bugs (9 KNOWNBUG tests)

1. `async-race-undetected` — sequential async model misses races (has opt-in fix via `--ts-async-threading`)
2. `integration-url-parser` — nested symbolic string operations exceed solver capacity
3. `generic-heterogeneous-tuple` — `[A, B]` return with mixed A, B types fails
4. `array-pop-return` — pop() return value not tracked through assignment
5. `array-sort-comparator` — sort with custom comparator not implemented
6. `enum-string-values` — string enum values not tracked
7. `set-has` — Set.has not tracking membership
8. `nullish-coalescing` — `??` not evaluating correctly for undefined LHS
9. `optional-chaining` — `?.` not short-circuiting on missing properties

## Overall assessment

**Well supported (works for 90%+ of real code):**
- Primitive types, arithmetic, comparisons
- Classes, inheritance, instanceof
- Functions, arrow functions, closures
- Interfaces, type aliases, union/intersection
- Narrowing via typeof, instanceof, in, discriminated union
- Generics via monomorphization
- Arrays (most methods)
- Strings (most methods)
- Maps, basic Set operations
- Promises (sequential model)
- Try/catch, error handling
- Imports/exports

**Partial (works for common patterns, gaps on edges):**
- Float-based loops (need `--no-unwinding-assertions` or `--ts-integer-mode`)
- Complex mapped/conditional types
- Iteration over Map/Set
- String Unicode edge cases
- Default values in destructuring
- Abstract class enforcement

**Not supported (see KNOWNBUG list):**
- Nullish coalescing and optional chaining
- Dynamic imports
- String enums
- Some array mutations (sort with comparator, pop return)
- Heterogeneous tuple returns from generic functions
- String-to-number coercion via unary `+`

## Recommended workarounds

- **Loops failing to unwind**: use `--no-unwinding-assertions` or `--ts-integer-mode`
- **Nullish/optional**: avoid `??`/`?.`, use explicit `if` checks
- **String enums**: use numeric enums with const strings outside
- **Sort with comparator**: use manual loop-based sort
- **Array.isArray**: now works after 2026-05-07 fix

## For contributors

When adding a new feature:
1. Record the ES2024 / TSH reference in the implementation
2. Add a CORE regression test demonstrating success
3. If there's a known limitation, add a KNOWNBUG test documenting it
4. Update this matrix with status and notes
