# TypeScript Frontend Capability Matrix

Status of each ES2024 / TypeScript Handbook feature in the CBMC TypeScript
frontend as of 2026-05-07. Compiled from systematic code review and
probe-testing against the specification.

Each row cites one or more regression tests under
`regression/typescript/` that demonstrate the capability (for supported
features) or document the gap (for `[KNOWNBUG]` features). Every listed
test name has been verified to exist. Where no dedicated test exists,
the column shows `—`.

**Legend:**
- ✅ Supported — feature works correctly
- ⚠️  Partial — feature works in common cases but has documented gaps
- ❌ Not supported — feature returns wrong result or nondet
- ⏳ Limited — feature works but requires workarounds (`--unwind`,
  `--ts-integer-mode`, `--no-unwinding-assertions`)

## Language fundamentals

| Feature | Status | Notes | Test(s) |
|---------|--------|-------|---------|
| Numeric literals | ⚠️ | BigInt not modeled | `number-methods`, `number-infinity` |
| String literals | ✅ |  | `string-length`, `string-equality` |
| Boolean literals | ✅ |  | `boolean-logic`, `boolean-not` |
| Template literals | ✅ | With interpolation | `template-literal`, `template-literal-complex` |
| `undefined`, `null` | ✅ | Both modeled as NaN sentinel; nullish operators (`??`) work; optional chaining (`?.`) limited | `null-safety`, `null-union-function`, `nullish-coalescing` |
| Variable declarations | ✅ |  | (covered implicitly in all tests) |
| Arithmetic operators | ✅ |  | `verify-modular-arithmetic`, `power-math` |
| Comparison operators | ✅ | NaN handling per IEEE 754 | `number-comparison`, `nan-check-div` |
| Logical operators | ✅ |  | `boolean-logic` |
| Bitwise operators | ✅ | Int32 conversion semantics | `bitwise-ops` |
| Assignment operators | ✅ |  | `compound-assignment` |
| typeof operator | ✅ | Literal-types case fixed 2026-05-07 | `typeof-number`, `typeof-function`, `typeof-literal-types` |
| instanceof operator | ✅ |  | `instanceof-check` |
| in operator | ✅ | Including narrowing | `in-operator` |
| Conditional operator (`? :`) | ✅ |  | `ternary`, `ternary-nested`, `ternary-number` |
| Nullish coalescing (`??`) (ES2024 §13.13) | ✅ | Fixed 2026-05-07 via NaN sentinel | `nullish-coalescing` |
| Optional chaining (`?.`) (ES2024 §13.3.9) | ❌ | Doesn't short-circuit | `optional-chaining` [KNOWNBUG] |
| String-to-number coercion (`+s`) | ❌ | `+"42"` doesn't parse | — |
| Number-to-string coercion (in `+`) | ✅ | Fixed 2026-05-07 | `string-concat-number-coerce` |
| Boolean-to-string coercion (in `+`) | ✅ | Fixed 2026-05-07 | `string-concat-number-coerce` |

## Control flow

| Feature | Status | Notes | Test(s) |
|---------|--------|-------|---------|
| `if` / `else` | ✅ |  | `if-else` |
| `switch` / `case` / `default` | ✅ |  | `switch-case`, `switch-default`, `enum-switch` |
| `for` (classic) | ⏳ | Float counter → `--no-unwinding-assertions` | `for-loop`, `for-loop-sum` |
| `for..of` | ✅ |  | `for-of-array`, `for-of-loop`, `verify-for-of-array` |
| `for..in` | ⚠️ | Array keys iteration limited | `for-in-basic` |
| `while` | ✅ | Loop-counter caveat | `while-loop`, `while-loop-complex`, `while-nondet` |
| `do..while` | ✅ | Loop-counter caveat | `do-while`, `verify-do-while` |
| `break`, `continue` | ✅ | Labeled variants | `for-break-continue`, `labeled-break`, `labeled-continue` |
| `try` / `catch` | ✅ |  | `try-catch` |
| `try` / `finally` | ✅ |  | `try-finally` |
| `throw` | ✅ |  | `throw-statement` |

## Functions

| Feature | Status | Notes | Test(s) |
|---------|--------|-------|---------|
| Function declarations | ✅ |  | `function-basic`, `function-no-return` |
| Arrow functions | ✅ |  | `arrow-function` |
| Default parameters | ✅ |  | `destructure-defaults` |
| Rest parameters | ✅ |  | `rest-params`, `rest-params-empty` |
| Spread in calls | ✅ |  | `spread-call-args`, `verify-spread-function` |
| Overloads | ⚠️ | Declaration accepted | `function-overload` |
| Closures | ✅ | Multi-closure mutable capture | `verify-closure-basic`, `verify-closure-adder`, `multi-closure-sharing` |
| Recursion | ✅ |  | (covered in `verify-*` function tests) |
| `this` in methods | ⚠️ | Rebinding limited | `class-basic` |

## Classes (TSH: Classes)

| Feature | Status | Notes | Test(s) |
|---------|--------|-------|---------|
| Class declarations | ✅ |  | `class-basic` |
| Constructors | ✅ |  | `class-basic`, `class-factory` |
| Instance methods | ✅ |  | `class-method-chain`, `class-chain-calls` |
| Static methods | ✅ |  | `static-method` |
| Getters / setters | ✅ |  | `class-getter`, `setter-property` |
| Private fields | ✅ | Enforced at type-check | `private-field-access`, `private-field-violation` |
| Inheritance (`extends`) | ✅ |  | `class-inheritance`, `verify-class-inheritance-override` |
| `instanceof` | ✅ |  | `instanceof-check` |

## Generics (TSH: Generics)

| Feature | Status | Notes | Test(s) |
|---------|--------|-------|---------|
| Generic functions | ✅ | Monomorphization | `generic-identity`, `generic-function`, `generic-first` |
| Generic classes | ✅ | Monomorphization | `generic-class-single`, `generic-class-multi` |
| Constraints (`T extends X`) | ✅ |  | `generic-constraint`, `generic-constraint-name` |
| Heterogeneous tuples | ❌ | Type unification fails | `generic-heterogeneous-tuple` [KNOWNBUG] |
| Utility types | ✅ | Via TS compiler | `utility-pick`, `utility-omit`, `utility-readonly` |

## Type system

| Feature | Status | Notes | Test(s) |
|---------|--------|-------|---------|
| Interfaces | ✅ |  | `interface-basic`, `interface-method`, `interface-nested` |
| Type aliases | ✅ |  | `type-alias-basic` |
| Union types | ✅ | Tagged union struct | `union-type-basic`, `union-type-function`, `union-assignment` |
| Discriminated unions | ✅ | Merged struct | `discriminated-union` |
| Literal types | ✅ |  | `template-literal-type`, `typeof-literal-types` |
| Template literal types | ✅ |  | `template-literal-type` |
| Optional properties | ✅ |  | `interface-optional` |
| Readonly properties | ✅ |  | `readonly-interface`, `readonly-const` |
| Type assertions | ✅ |  | `type-assertion` |
| Type guards (user-defined) | ⚠️ | Non-union params supported | `type-guard-union` |

## Narrowing (TSH: Narrowing)

| Feature | Status | Notes | Test(s) |
|---------|--------|-------|---------|
| `typeof` narrowing | ✅ | Literal types fixed | `type-narrowing-typeof`, `typeof-literal-types` |
| `instanceof` narrowing | ✅ |  | `instanceof-check` |
| `in` operator narrowing | ✅ |  | `in-operator` |
| Discriminated union narrowing | ✅ |  | `discriminated-union` |
| User-defined guards | ⚠️ |  | `type-guard-union` |

## Enums (TSH: Enums)

| Feature | Status | Notes | Test(s) |
|---------|--------|-------|---------|
| Numeric enums (auto) | ✅ |  | `enum-basic`, `enum-explicit` |
| Numeric enums (explicit) | ✅ |  | `enum-values`, `enum-explicit` |
| Enum arithmetic / bitflags | ✅ |  | `enum-arithmetic`, `enum-bitflags` |
| Enums in switch | ✅ |  | `enum-switch` |
| Enums in condition | ✅ |  | `enum-condition` |
| String enums | ✅ | Fixed 2026-05-07 | `enum-string-values` |

## Destructuring

| Feature | Status | Notes | Test(s) |
|---------|--------|-------|---------|
| Array destructuring | ✅ |  | `array-destructure-basic`, `array-destructuring` |
| Array destructuring with rest | ✅ |  | `array-destructure-rest` |
| Destructuring defaults | ⚠️ |  | `destructure-defaults` |
| Object destructuring (plain) | ✅ |  | `object-destructure`, `object-destructuring` |
| Object destructuring with rename | ✅ | Fixed 2026-05-07 | `object-destructuring-rename` |
| Object spread | ✅ |  | `object-spread`, `object-spread-override` |

## Built-in types

### Array (ES2024 §23.1)

| Method | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| `length` | ✅ |  | `array-length-check`, `array-length-nondet` |
| `push` | ✅ |  | `array-empty-push`, `spread-and-push` |
| `pop` return | ✅ | Fixed 2026-05-07 | `array-pop-return` |
| `map`, `filter`, `reduce` | ✅ |  | `array-map-arrow`, `array-filter`, `array-reduce` |
| `forEach` | ⚠️ | (no dedicated test) | — |
| `find`, `findIndex` | ✅ |  | `array-find`, `array-findIndex` |
| `includes`, `indexOf` | ✅ | Non-const via symbolic scan | `array-includes`, `array-indexOf` |
| `every`, `some` | ✅ |  | `array-every`, `array-every-fail` |
| `slice`, `splice` | ✅ |  | `array-splice` |
| `concat` | ✅ |  | `array-concat` |
| `flat` | ✅ |  | `array-flat` |
| `join` | ✅ |  | `array-join` |
| `reverse`, `fill` | ✅ |  | `array-fill` |
| `at` | ✅ |  | `array-at` |
| `sort` (with comparator) | ✅ | Fixed 2026-05-07 (recognizes `(a,b)=>a-b` and `(a,b)=>b-a` patterns for constant arrays) | `array-sort-comparator` |
| `Array.from` | ✅ |  | `array-from`, `array-from-pattern` |
| `Array.isArray` | ✅ | Fixed 2026-05-07 | `array-is-array` |
| Spread `[...arr]` | ✅ |  | `array-copy-spread`, `spread-and-push` |

### String (ES2024 §22.1)

**Symbolic-string soundness note.** Non-literal strings (e.g.
`nondet_string()`, function parameters typed `string`) have
nondeterministic content from the solver's perspective. Length-based
properties verify precisely; content-based predicates on symbolic
receivers (e.g. `s.includes("ell")` when `s === "hello"`) fail to
verify even when they hold concretely. This is sound (no false
positives) but imprecise. Constant strings are fully evaluated at
conversion time and remain character-precise. See
`doc/typescript-verification-guide.md` §Strings for details.

| Feature | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| `length` | ✅ | Precise on symbolic too | `string-length`, `string-length-check` |
| Concatenation (`+`) | ✅ | Constant: precise; Symbolic: length-precise, content nondet | `string-concat`, `string-concat-multi` |
| Concatenation (`concat` method) | ✅ | Fixed 2026-05-07 (constant); symbolic as `+` | `string-concat-method` |
| Split | ✅ | Constant only | `string-split` |
| Replace / replaceAll | ✅ | Constant only | `string-replace`, `string-replaceAll`, `string-replaceAll-multi` |
| Slice | ✅ | Constant args only | `string-slice`, `string-slice-negative` |
| Substring (with spec-compliant swap) | ✅ | Constant args only | `string-substring-swap` |
| Repeat | ✅ | Safety-capped at 10000; symbolic content nondet | `string-repeat` |
| String-number conversion | ⚠️ |  | `string-number-convert` |
| Multi-method chains | ✅ | On constants | `string-methods-chain`, `string-methods-combined` |
| Comparison (`===`, `!==`) | ✅ | Per-slot struct compare; works on symbolic | `string-comparison-ops`, `string-equality` |
| `indexOf` (with fromIndex) | ✅ | fromIndex fixed 2026-05-07; constant only | `string-indexof-fromindex` |
| `lastIndexOf` | ✅ | Constant only | `string-last-index-of` |
| `startsWith` / `endsWith` (with position) | ✅ | Position arg fixed 2026-05-07 (constant). Symbolic over-approximates content | `string-starts-ends-position` |
| `includes` on symbolic strings | ⚠️ | Over-approximates: content is nondet in the solver | `string-includes-symbolic` [KNOWNBUG] |
| `startsWith` / `endsWith` on symbolic strings | ⚠️ | Over-approximates as above | `string-startswith-symbolic` [KNOWNBUG] |
| `toUpperCase` / `toLowerCase` on symbolic strings | ⚠️ | Length precise, content nondet | `string-case-symbolic` [KNOWNBUG] |
| `trim` on symbolic strings | ⚠️ | Length bound (`<= input.length`) precise; content nondet | `string-trim-symbolic` [KNOWNBUG] |
| `padStart` / `padEnd` (short-circuit) | ✅ | Fixed 2026-05-07 | `string-pad-short-circuit` |
| `padStart` / `padEnd` (multi-char pad) | ✅ | Fixed 2026-05-07 | `string-pad-multichar` |
| `trim` (constant) | ✅ |  | (covered in `string-methods`) |
| `trimStart` / `trimEnd` | ✅ | Added 2026-05-07; constant | `string-trim-start-end` |
| `charCodeAt`, `String.fromCharCode` | ❌ | Not implemented | — |
| Empty-string receiver | ⚠️ | Most methods bypass empty-string receivers | — |

### Map (ES2024 §24.1)

| Method | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| `set`, `get`, `has` | ✅ |  | `map-set-basic`, `map-get-constant`, `map-set-operations` |
| `size` | ✅ |  | `map-set-basic` |
| Iteration | ⚠️ |  | `map-iteration` |
| Chained filter/transform | ✅ |  | `map-filter-chain` |

### Set (ES2024 §24.2)

| Method | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| `add`, `delete`, `size` | ✅ |  | `map-set-basic` (Set constructor path) |
| `has` | ✅ | Fixed 2026-05-07 (numeric Sets; string Sets limited by data-type hardcoding) | `set-has` |
| Iteration | ⚠️ |  | `set-iteration` |

### Math (ES2024 §21.3)

| Method | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| Basic methods (abs, floor, ceil, round, sqrt, pow) | ✅ |  | `math-builtins`, `math-functions` |
| `max`, `min` (variadic) | ✅ | Fixed 2026-05-07 | `math-max-min-variadic`, `math-min-max` |
| `random` | ⚠️ | Nondet in [0, 1) | `math-random` |

### Number (ES2024 §21.1)

| Feature | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| `isInteger`, `isNaN`, `isFinite` | ✅ |  | `number-methods` |
| `Infinity`, sign zero | ✅ |  | `number-infinity` |
| Comparison | ✅ |  | `number-comparison` |

### Error (ES2024 §20.5)

| Feature | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| `throw new Error(msg)` | ✅ |  | `throw-statement`, `try-catch` |
| `try`/`catch` | ✅ |  | `try-catch`, `try-finally` |

### Promise (ES2024 §27.2)

| Feature | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| `Promise.resolve(x)` | ✅ |  | `promise-resolve` |
| `.then` / `.catch` | ✅ |  | `promise-then`, `promise-catch` |
| `Promise.all` | ✅ | Sequential | `async-promise-all` |
| `async` / `await` (sequential) | ✅ | Default | `async-basic`, `async-await-value`, `async-sequential-order`, `async-then-chain` |
| `async` / `await` (threading) | ⏳ | Opt-in `--ts-async-threading` | `async-race-detected`, `async-race-undetected` [KNOWNBUG] |

## Modules (TSH: Modules)

| Feature | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| `import { x } from "mod"` | ✅ |  | `multi-file-import` |
| Re-export | ⚠️ |  | `multi-file-reexport` |
| Dynamic `import()` | ❌ | Not implemented | — |

## TSX / JSX

| Feature | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| `.tsx` parsing | ✅ |  | `tsx-extension` |

## Decorators

| Feature | Status | Notes | Test(s) |
|--------|--------|-------|---------|
| Basic decorator | ✅ |  | `decorator-basic` |
| Parameterized decorator | ✅ |  | `decorator-parameterized` |

## CBMC-specific extensions

| Feature | Status | Notes | Test(s) |
|---------|--------|-------|---------|
| `__CPROVER_assume` | ✅ |  | `array-length-nondet` + many |
| `__CPROVER_loop_invariant` | ✅ |  | `verify-binary-search-invariant`, `verify-stack-invariant` |
| `__CPROVER_requires` / `_ensures` | ✅ |  | `precondition` |
| `nondet_number()`, `nondet_boolean()`, `nondet_string()` | ✅ |  | `array-length-nondet`, many others |
| `--ts-integer-mode` | ✅ | int64 instead of float64 | `verify-binary-search-invariant` |
| `--ts-async-threading` | ✅ | Async interleaving via CBMC threads | `async-race-detected` |
| `--nan-check` | ✅ |  | `nan-check-div`, `nan-check-fail` |

## KNOWNBUG tests (12)

**Design trade-offs (3):**

| Test | Symptom | ES2024 / TSH ref |
|------|---------|------------------|
| `async-race-undetected` | Sequential async misses unobserved-race bugs (opt-in fix via `--ts-async-threading`) | §27.2 |
| `object-prototype-chain` | Our struct model has no prototype chain; `getPrototypeOf` / `isPrototypeOf` not modelled | §20.1 |
| `strict-nan-not-equal` | `NaN === NaN` returns `true` in our null-as-NaN model (spec says `false`) | §7.2.14 |

**Symbolic-string content over-approximation (6):**

The refined-string solver sees nondet content for any non-literal
string. Length properties verify precisely; content properties on
symbolic receivers fail to verify even when they hold concretely
(sound over-approximation). Restoring precision would require
switching our string struct from fixed inline array to heap-pointer
representation.

| Test | Symptom | ES2024 / TSH ref |
|------|---------|------------------|
| `string-case-symbolic` | `s === "hello"` ⇒ `s.toUpperCase() === "HELLO"` not provable (content nondet) | §22.1.3.30 |
| `string-concat-symbolic-content` | `a === "foo"` ∧ `b === "bar"` ⇒ `(a+b) === "foobar"` not provable | §22.1.3.3 |
| `string-includes-symbolic` | `s === "hello"` ⇒ `s.includes("ell")` not provable | §22.1.3.7 |
| `string-startswith-symbolic` | `s === "hello"` ⇒ `s.startsWith("he")` not provable | §22.1.3.23 |
| `string-symbolic-realistic` | Combines several of the above on realistic code | §22.1 |
| `string-trim-symbolic` | `s === "  hi  "` ⇒ `s.trim() === "hi"` not provable | §22.1.3.32 |

**Precision probes (3):**

| Test | Symptom | Notes |
|------|---------|-------|
| `array-push-length-in-loop` | Symbolic array length tracking imprecise through loops | Unresolved design question |
| `higher-order-compose` | Nested function composition loses type information | Monomorphisation limitation |
| `string-concat-chained-in-function` | Chained concat results lose length precision across function boundaries | Related to symbolic string content over-approximation |

## Recently fixed bugs (CORE tests guard against regression)

The bugs below were found during spec cross-referencing and the soundness
review. They do NOT have `[KNOWNBUG]` tests because each was fixed in
the same commit that discovered it. The CORE tests below serve as the
regression guards — if the fix regresses, the CORE test fails and CI
catches it.

| CORE test | Was broken | Fixed in commit |
|-----------|-----------|-----------------|
| `typeof-literal-types` | `typeof 42 === "object"` (literal types fell through) | 3325a425d7 |
| `string-concat-number-coerce` | `"x" + 1` lost the number operand (type promotion miscast string to float) | 3325a425d7 |
| `object-destructuring-rename` | `const { a: renamed } = obj` didn't resolve `propertyName` | 83dd85424d |
| `array-is-array` | `Array.isArray([1,2])` returned nondet | 508444ebb9 |
| `math-max-min-variadic` | `Math.max(1, 2, 3)` only used first two args | 508444ebb9 |
| `array-pop-return` | pop() return value overwritten by length update | 227e96573e |
| `set-has` | Set.has returned nondet (no membership tracking) | 227e96573e |
| `enum-string-values` | String enum members stored as numeric indexes | f15b3fd719 |
| `array-sort-comparator` | sort with comparator was a no-op | f15b3fd719 |
| `nullish-coalescing` | `??` always returned LHS (undefined model wrong) | 38a8e3af91 |
| `string-pad-short-circuit` | padStart/padEnd truncated when length ≥ target | e0b7c2e7e2 |
| `string-trim-start-end` | trimStart / trimEnd not implemented | e0b7c2e7e2 |
| `string-substring-swap` | substring didn't swap start/end per spec | 85943286fc |
| `string-indexof-fromindex` | indexOf ignored fromIndex arg | 85943286fc |
| `string-starts-ends-position` | startsWith/endsWith ignored position arg | 85943286fc |
| `string-concat-method` | String.prototype.concat not implemented | fa68297ded |
| `string-last-index-of` | String.prototype.lastIndexOf not implemented | fa68297ded |
| `string-pad-multichar` | Multi-char pad pattern not truncated per spec | fa68297ded |

## Overall assessment

**Well supported** (works for 90%+ of real code): primitives, classes,
generics, narrowing, most Array/String methods on constants, Promises,
imports/exports.

**Partial:** float-loop unwinding, mapped/conditional types, Map/Set
iteration, Unicode edge cases, destructuring defaults, abstract class
enforcement.

**Sound over-approximation** (see KNOWNBUG): content-based predicates
on symbolic strings (`s.includes(x)`, `s.startsWith(x)`,
`s.toUpperCase() === "…"`, `(a+b) === "…"`). Length properties on
symbolic strings remain precise.

**Not supported** (see KNOWNBUG): dynamic imports, heterogeneous
tuple returns.

## Workarounds

- **Float-loop unwind**: `--no-unwinding-assertions` or `--ts-integer-mode`
- **Nullish / optional chaining**: explicit `if` checks
- **String enums**: numeric enums + lookup table
- **Sort with comparator**: manual loop-based sort
- **Set membership**: use `Map<T, boolean>` instead
- **Async races**: `--ts-async-threading`

## For contributors

When adding a feature:
1. Cite the ES2024 / TSH reference in the implementation
2. Add a CORE regression test demonstrating success
3. If there's a known limitation, add a KNOWNBUG test
4. Update this matrix with status, notes, AND a test reference pointing to
   an existing regression test

When fixing a bug:
1. Add a CORE test that would have caught the bug (replaces any prior
   KNOWNBUG that documented the issue — delete the KNOWNBUG)
2. Update the "Recently fixed" table with commit hash and symptom
