# TypeScript Verification Guide

CBMC includes a TypeScript front-end that verifies TypeScript programs
using bounded model checking. It uses the TypeScript Compiler API for
parsing and type-checking, then converts the typed AST to GOTO programs
for verification.

The front-end is under active development. The regression suite covers
671 programs (including 9 new tests from ES2024 spec review pass 2
that fixed ToBoolean on strings, parseInt/Number parsing,
Array.forEach, Math.hypot/imul/clz32/log1p/expm1, and destructuring
defaults). Three tests are marked `KNOWNBUG`, all of them
pre-existing design trade-offs (see the "Design trade-offs" section
at the end of this guide). The full symbolic-string suite, precision
probes, and closure-capture suite are CORE and green.

## Contents

1. [Quick start](#quick-start)
2. [Verification primitives](#verification-primitives)
3. [Supported language features](#supported-language-features)
4. [Performance: the parse daemon](#performance-the-parse-daemon)
5. [Example: binary search](#example-binary-search)
6. [Multi-file projects](#multi-file-projects)
7. [Verification flags](#verification-flags)
8. [Known limitations](#known-limitations)
9. [References](#references)

## Requirements

- Node.js (v18+) with the `typescript` package installed globally (the
  frontend looks for it under `/usr/local/lib/node_modules/typescript`).
- CBMC built with TypeScript support (the `typescript` subdirectory
  in `src/`).

## Quick start

```typescript
// example.ts
const x: number = nondet_number();
__CPROVER_assume(x >= 0 && x <= 100);
const y: number = x * 2;
console.assert(y <= 200);
```

```bash
cbmc example.ts
# => VERIFICATION SUCCESSFUL
```

When CBMC is invoked on a `.ts` / `.tsx` file, it automatically enables
`--refine-strings` unless a conflicting SMT backend is selected
(`--z3` / `--smt2`). You can opt out with `--no-refine-strings` if
needed for debugging.

## Verification primitives

### Nondeterministic values

```typescript
declare function nondet_number(): number;
declare function nondet_boolean(): boolean;
declare function nondet_string(): string;
declare function nondet_array(): number[];
```

These return unconstrained symbolic values. CBMC verifies that the
property holds on every reachable path for every possible value.

### Assumptions and assertions

```typescript
__CPROVER_assume(condition);    // constrain nondet to paths where cond holds
__CPROVER_assert(condition);    // CBMC checks this must always hold
console.assert(condition);      // same as __CPROVER_assert
console.assert(cond, "reason"); // the reason string is stored as metadata
```

### Contracts

```typescript
__CPROVER_requires(cond);       // function precondition
__CPROVER_ensures(cond);        // function postcondition
__CPROVER_loop_invariant(cond); // loop invariant
__CPROVER_cover(cond);          // coverage goal
```

## Supported language features

### Types

| Type | Model |
|------|-------|
| `number` | IEEE 754 double-precision float (`floatbv[64]`). Null and undefined share the NaN representation. |
| `boolean` | `bool`. |
| `string` | Fixed-size struct `{length: signedbv[32], data: unsignedbv[16][64]}` tagged as `__CPROVER_refined_string_type` so the string solver can recognise it. Most operations are evaluated at conversion time for constants; symbolic operations use per-case encodings and the refined string solver (see [Strings](#strings)). |
| `T[]` / `Array<T>` | Fixed-size struct `{length: int64, data: T[16]}` (array length capped at `TYPESCRIPT_MAX_ARRAY_LENGTH`). |
| `[T, U, V]` tuples | Heterogeneous tuples become `typescript_tuple` structs; homogeneous tuples collapse to array-of-T. |
| `Map<K, V>` / `Set<T>` | Bounded-entry structs with `set/add`, `get`, `has`, `delete`, `size`. Default capacity: 8 entries. |
| Object literals / interfaces | Plain structs. Optional fields (`x?: number`) initialise to NaN. |
| Classes | Structs with methods. Supports inheritance, constructors, private fields. |
| Enums | Numeric values (via monomorphisation of the numeric constants). |
| Discriminated unions | `typescript_union` struct with a tag field and per-variant slots. `typeof` narrowing is supported. |
| Generics | Monomorphised at call sites. Multi-type-param (`<A, B>`) supported. |

### Declarations and expressions

- `const` and `let` with type annotations; type inference across
  initializers.
- Arithmetic: `+`, `-`, `*`, `/`, `%`.
- Comparison: `===`, `!==`, `<`, `>`, `<=`, `>=`, `==`, `!=`.
- Logical: `&&`, `||`, `!`.
- Compound assignment: `+=`, `-=`, `*=`, `/=`, etc.
- Increment / decrement: `++`, `--`.
- Ternary: `condition ? a : b`.
- Nullish coalescing: `??` and `??=`.
- Optional chaining: `obj?.x`, `arr?.[0]`.
- Destructuring: `const { x, y = 99 } = obj`, `const [a, b] = arr`.
  Default-value initializers work when the source field is undefined.
- Spread: `const c = [...a, ...b]`, `const d = { ...o1, ...o2 }`.
- Template literals: `` `prefix ${expr} suffix` `` — constant expressions
  (including unary minus and arithmetic) are folded at conversion time.
- `typeof x === "string"` narrowing for discriminated unions.
- `x as T` / angle-bracket casts.

### Control flow

- `if` / `else`.
- `while`, `do-while`, `for`, `for...of`, `for...in`.
- `switch` / `case` / `default`.
- `break` / `continue` / labelled break.
- `try` / `catch` / `finally` — throw statements are modelled; `catch`
  binds the exception value.

### Functions

- Function declarations, arrow functions, function expressions.
- Optional parameters with defaults.
- Rest parameters (`...args`).
- Nested functions with closure capture.
- Generic functions (including multi-type-param generics).
- Promises and `async` / `await` — sequential model by default, with
  an opt-in threading model via `--ts-async-threading` (see
  [Known limitations](#known-limitations)).

### Classes

- Property declarations, constructors, methods.
- Inheritance (`extends`) and `super()` in constructors and methods.
- Private fields via `private` modifier or `#name` syntax — access from
  outside the class triggers a verification failure.
- Static members.
- Getters and setters.
- Generic classes (including multi-type-param).

Not yet supported:

- Parameter-property shorthand (`constructor(public x: number) {}`).
  Write it out with an explicit property + assignment in the constructor.
- Abstract classes.
- Decorators.

### Arrays

- Literals, indexing with bounds check (on by default).
- `length` property (constant for literals, symbolic after mutation).
- Mutation: `push`, `pop`, `shift`, `unshift`, `splice` (including
  symbolic `deleteCount`), `sort` (including symbolic elements via a
  bubble-sort compare-and-swap network).
- Functional: `map`, `filter`, `reduce`, `find`, `findIndex`, `some`,
  `every`, `forEach`.
- Queries: `includes`, `indexOf`, `lastIndexOf`, `at`, `join`.
- Slicing: `slice`, `concat`, `reverse`.
- Array.isArray, Array.from.
- `flat()` on 1-D arrays (no-op).
- Spread and destructuring.
- `for...of` iteration.

### Strings

Most common operations are supported. For **constant** strings, all
work is done at conversion time (fast, precise). For **symbolic**
strings, a subset of operations route through CBMC's refined string
solver.

**Content precision on symbolic strings.** The solver receives the
actual character content of non-literal strings (`nondet_string()`,
parameters typed `string`, results of other string operations).
Content-based properties verify precisely: `s === "hello"` ⇒
`s.includes("ell")`, `s.toUpperCase() === "HELLO"`,
`s + "bar" === "foobar"`, etc. Multi-assertion programs are fully
supported: the refined-string axioms are generated for every
`cprover_string_*` call regardless of how many assertions the
program has (a CBMC-core bug that previously discarded those axioms
under the multi-assertion BMC path was fixed 2026-05-12).

**Scalability cap.** The SAT encoding of a full content comparison
on a long symbolic receiver combined with several chained solver
operations can exceed the default memory envelope. If you hit
"Out of memory" or "VERIFICATION ERROR / current index set is
empty", raise `ulimit -v` or split the assertions across
independent receivers. Two tests (`string-trim-symbolic`,
`string-symbolic-realistic`) are documented KNOWNBUGs for the
combined trim+content compare and chained-operation patterns
respectively.

| Operation | Constant | Symbolic |
|-----------|---------|----------|
| `s.length` | ✓ | ✓ |
| `s.charAt(i)` | ✓ | ✓ (for constant `i`) |
| `s.charCodeAt(i)` | ✓ | ✓ (for constant `i`) |
| `s.indexOf(x)` | ✓ | ✓ (bounded-needle match chain) |
| `s.lastIndexOf(x)` | ✓ | — |
| `s.includes(x)` | ✓ | ✓ character-precise |
| `s.startsWith(x)` | ✓ | ✓ character-precise |
| `s.endsWith(x)` | ✓ | ✓ character-precise |
| `s.substring`, `s.slice` | ✓ | ✓ (for constant args) |
| `s.toUpperCase` | ✓ | ✓ character-precise |
| `s.toLowerCase` | ✓ | ✓ character-precise |
| `s.trim` | ✓ | length-precise; content precise but scales poorly on content === compare |
| `s.repeat(n)` | ✓ | length-precise (`n * s.length`); content nondet |
| `s.padStart / padEnd` | ✓ | length-precise (`max(s.length, n)`); content nondet |
| `s.replace / replaceAll` | ✓ | — |
| `s.split(delim)` | ✓ | ✓ for `split("")` (per-char) |
| `+s` (ToNumber) | ✓ | length-bounded (via `cprover_string_parse_int_func`) |
| `s1 + s2` | ✓ | ✓ character-precise (via `cprover_string_concat_func`) |
| `s1 === s2` | ✓ | ✓ (native struct compare, per-slot) |
| `String.fromCharCode(code)` | ✓ | — |
| `JSON.parse` / `JSON.stringify` | ✓ primitives, arrays, one-level nested objects | length-only for symbolic args |

Template literals work for any interpolation that constant-folds
(numbers via unary minus, `+`, `-`, `*`, `/`; booleans; other strings).

### Math

- `Math.abs`, `Math.sign`.
- `Math.floor`, `Math.ceil`, `Math.round`, `Math.trunc` — use
  `floatbv_round_to_integral_exprt` for symbolic inputs.
- `Math.sqrt`, `Math.cbrt`, `Math.log`, `Math.exp`.
- `Math.sin`, `Math.cos`, `Math.tan`, `Math.asin`, `Math.acos`, `Math.atan`, `Math.atan2`.
- `Math.min`, `Math.max`, `Math.pow`.
- `Math.PI`, `Math.E`.
- `Number.isNaN`, `Number.isFinite`, `Number.isInteger`, `Number.parseInt`, `Number.parseFloat`.

### JSON

- `JSON.stringify(value)` — primitives, arrays, nested objects. Symbolic
  numbers are stringified with a constrained nondet length (`≥ 1`).
- `JSON.parse(text)` — primitives, booleans, null (→ NaN), strings,
  arrays of primitives, and **one level** of nested objects.

### Object

Static methods (ES2024 §20.1.2):

- `Object.keys(o)`, `Object.values(o)`, `Object.entries(o)`
- `Object.fromEntries(entries)`
- `Object.assign(target, ...sources)`
- `Object.is(a, b)` — SameValue, distinguishing +0 from −0
- `Object.freeze(o)` / `Object.isFrozen(o)` — tracks frozenness per
  symbol. Frozen-status propagates through `const alias = Object.freeze(o)`.
- `Object.hasOwn(o, key)`

Prototype methods (§20.1.3):

- `o.hasOwnProperty(key)`

### Map and Set

```typescript
const m: Map<string, number> = new Map();
m.set("a", 1);
m.set("b", 2);
console.assert(m.has("a"));
console.assert(m.get("a") === 1);
console.assert(m.size === 2);

for (const [key, value] of m) {
  // ...
}
```

Default capacity: 8 entries per Map/Set. Override with
`--ts-max-array-size N` (applies to Maps, Sets, and arrays).

## Performance: the parse daemon

Parsing `.ts` files via the TypeScript compiler takes ~900 ms per
file due to loading `lib.d.ts`. For the 632-test regression suite
this would be ~10 minutes of pure parse time.

CBMC can use a long-running parse daemon that keeps a TypeScript
Language Service instance alive across invocations. With the daemon,
regression drops to **~33 seconds** (18× speedup).

### Starting and stopping

```bash
# One-shot
scripts/cbmc_ts_server start    # prints the socket path
scripts/cbmc_ts_server stop
scripts/cbmc_ts_server status

# Used automatically by tests via CMake or Makefile
ctest -R typescript             # CMake fixtures start/stop the daemon
make -C regression/typescript test   # Makefile target does the same
make -C regression/typescript test-no-daemon   # bypass for profiling
```

### Using the daemon manually

```bash
export CBMC_TS_SERVER_SOCKET=$(scripts/cbmc_ts_server start)
cbmc myfile.ts                  # now uses the daemon
scripts/cbmc_ts_server stop
```

If the socket is unset or unreachable, CBMC falls back to one-shot
node invocation — the daemon is fully optional.

### Reproducibility

The daemon holds no persistent state (no disk cache). If the daemon
is stopped, behaviour is identical to one-shot mode. This is
deliberate: for profiling (`scripts/profile_cbmc.py`) or
reproducibility-sensitive benchmarking, simply don't start the daemon.

## Example: binary search

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

## Multi-file projects

```typescript
// math.ts
export function add(a: number, b: number): number { return a + b; }

// main.ts
import { add } from './math';
console.assert(add(2, 3) === 5);
```

```bash
cbmc main.ts
```

Supported: named imports/exports, interface/class imports, enum imports,
default exports. Not supported: `node_modules` imports, dynamic imports
(`import()`), namespace imports (`import * as ns`).

## Verification flags

General CBMC flags that work with TypeScript:

```bash
cbmc --bounds-check file.ts             # array bounds (on by default)
cbmc --no-bounds-check file.ts          # disable bounds checks
cbmc --float-div-by-zero-check file.ts  # 0.0 / 0.0 → NaN is fine; check that
cbmc --nan-check file.ts                # detect NaN propagation
cbmc --unwind 10 file.ts                # loop unwinding limit
cbmc --no-unwinding-assertions file.ts  # don't treat N as hard bound
```

TypeScript-specific flags:

```bash
cbmc --ts-integer-mode file.ts          # use int64 for integer-only vars
cbmc --ts-max-array-size 32 file.ts     # increase default 16 array size
cbmc --ts-async-threading file.ts       # opt-in interleaving for async
cbmc --no-refine-strings file.ts        # bypass the string solver
```

`--ts-integer-mode` can be orders of magnitude faster for counter
loops, indices, and modulo arithmetic. Variables used with `%`, `&`,
`|`, `^`, `<<`, `>>` get `signedbv[64]`; variables used with `*` or
`/` remain float (to avoid overflow/fraction issues).

## Known limitations

Documented `KNOWNBUG` tests indicate cases where a design trade-off
intentionally gives an unsound or imprecise answer. All 3 remaining
KNOWNBUGs are design trade-offs.

### Design trade-offs

1. **`async-race-undetected`** — by default our async model is
   sequential: `await` serialises tasks. A race condition between
   concurrent `async` functions won't be detected unless you pass
   `--ts-async-threading`, which unlocks the interleaving model at
   the cost of much higher verification time.

2. **`object-prototype-chain`** — `Object.getPrototypeOf` and
   `isPrototypeOf` are not modelled. Our struct model has no
   prototype chain; classes are represented as flat structs.

3. **`strict-nan-not-equal`** — per ES2024 §7.2.14, `NaN === NaN`
   is false. Our frontend represents `null`, `undefined`, and `NaN`
   all as IEEE-754 NaN, so `NaN === NaN` returns true in exchange
   for correct `null === null` and `undefined === undefined`.

### Other documented edge cases

- `Object.is(+0, -0)` returns `false` per ES2024 for constant zeros,
  but only in the constant path — symbolic zero-sign tracking is not
  available.
- `for-of` over a string (`for (const c of "abc")`) is a no-op with a
  warning. Use indexed iteration as the workaround:
  `for (let i = 0; i < s.length; i++) s.charAt(i)`.
- Ternary expressions whose branches yield a literal-union type
  (e.g. the TS-inferred `10 | 20` from `0 ? 10 : 20`) are imprecise
  because our tagged-union representation can't reliably match the
  target integer. Annotate the binding explicitly (`const b: number
  = 0 ? 10 : 20`) to bypass literal-union inference.
- Mixed-union-type arrays (e.g. `(number | number[])[]`) crash
  CBMC's `simplify_member` invariant during constant-fold passes.
  Uniform nested arrays (`number[][]`) work correctly.
- Modules beyond `./relative` imports (e.g. `node_modules`) are not
  supported.
- RegExp is not modelled.
- BigInt is not modelled.
- Date is not modelled.
- Decorators are not modelled.

## References

### In this repository

- `doc/architectural/typescript-frontend-plan.md` — frontend design
- `doc/architectural/typescript-multifile-plan.md` — module resolution
- `doc/typescript-capability-matrix.md` — per-feature support matrix
- `doc/typescript-performance-report.md` — performance numbers
- `doc/refined-string-migration-plan.md` — string solver integration
- `doc/over-approximation-audit.md` — unsoundness audit, all resolved
  or documented
- `doc/fuzz-durability-report.md` — fuzzer findings

### Regression tests

Over 600 test programs in `regression/typescript/` exercise every
documented feature. Tests named `*-symbolic` cover nondeterministic
inputs; `integration-*` tests verify against real-world patterns.

### Fuzzing

Two fuzzers are available:

```bash
# Generator-based fuzzer (random programs, tsc as oracle)
python3 scripts/fuzz_typescript.py --iterations 100

# Property-based fuzzer (ES2024-grounded invariants)
python3 scripts/property_fuzz_typescript.py --iterations 200
```

Both pick up `CBMC_TS_SERVER_SOCKET` automatically if set.
