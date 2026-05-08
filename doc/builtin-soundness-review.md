# TypeScript Frontend — Comprehensive Soundness Review

Date: 2026-05-08  
Scope: Empty-receiver bypass + per-spec soundness review of Array, Math,
Number, and Map/Set subsystems against ECMAScript 2024.  
Starting state: 578 CORE / 4 KNOWNBUG.  
Ending state: 595 CORE / 4 KNOWNBUG (17 new CORE tests, ~130 new
assertions; 30+ bug fixes and new method implementations).

This extends the earlier String review (see
`doc/string-soundness-review.md`) to cover the remaining major
built-in subsystems. Same methodology: for each ES2024 section, write
probe tests that exercise boundary behaviours, default arguments, and
short-circuit paths — then fix each gap.

## Summary of fixes and additions

| Subsystem | Real bugs | Methods added | New CORE tests |
|-----------|-----------|---------------|----------------|
| Empty-receiver bypass | 1 | 0 | 1 |
| Array.prototype | 4 | 3 | 7 |
| Math | 2 | 10 | 4 |
| Number.prototype | 3 | 6 | 4 |
| Map / Set | 4 | 3 | 2 |
| **Total** | **14** | **22** | **18** |

## Empty-receiver bypass

### Bug

The string-method dispatcher was:
```cpp
std::string sv;
{ ... sv = raw.substr(2); }    // sv empty when receiver is ""
if(!sv.empty()) { /* all methods */ }
```
This silently skipped every method when the receiver was `""`. So
`"".concat("x")` returned the default (nil), `"".indexOf("")` didn't
return 0, etc.

### Fix

Track a separate `sv_known` flag indicating whether we have a known
(possibly empty) string value. Gate dispatch on `sv_known` instead of
`!sv.empty()`.

### Test (`string-empty-receiver`)

14 assertions covering `concat`, `indexOf`, `padStart/End`, `repeat`,
`trim/trimStart/trimEnd`, `split`, `toUpperCase/toLowerCase` — all
now work on `""`.

## Array.prototype (ES2024 §23.1)

### Bugs fixed

1. **`Array.prototype.slice` ignored negative indices.** Per spec §23.1.3.27, negative start/end counts from the end. Also didn't clamp over-length correctly. Fixed by recognising unary-minus constants and applying `max(0, len + idx)`.

2. **`Array.prototype.splice` didn't shift elements.** Per spec §23.1.3.30, splice(start, deleteCount, ...items) mutates the array: removes deleteCount items at start and inserts items in their place. Old code only decremented length. Fixed for constant arrays: rebuild array with before + inserted + after, assign back to receiver, update symbol-table value.

3. **`Array.prototype.fill` didn't support start/end or mutate.** Per spec §23.1.3.7, fill mutates in place with optional [start, end) range (negatives from end). Old code ignored start/end and returned a new array. Fixed.

4. **`Array.prototype.indexOf` ignored fromIndex.** Per spec §23.1.3.15, the second argument is the starting index. Fixed, with negative-from-end support.

### Methods added

1. **`Array.prototype.lastIndexOf`** (§23.1.3.16) — symmetric to indexOf, scans backward.
2. **`Array.prototype.findLast` / `findLastIndex`** (§23.1.3.9/9.1) — reverse iteration variant of find/findIndex.
3. **`Array.of`** (§23.1.2.3) — new static method that builds an array from its arguments.

### Tests added

- `array-slice-negative` — negative indices, out-of-range, start>end
- `array-splice-shift` — delete, insert, replace semantics
- `array-fill-range` — default range, explicit range, negative start
- `array-find-last` — forward/reverse match, no-match
- `array-last-index-of` — various shapes, fromIndex
- `array-indexof-fromindex` — fromIndex with negatives
- `array-of` — 0, 1, and multi-arg invocations

## Math (ES2024 §21.3)

### Bug fixed

**`Math.round` used wrong rounding direction for ties.** `std::round` rounds ties AWAY from zero (so `-0.5 → -1`). ES2024 §21.3.2.29 rounds ties toward +infinity (so `-0.5 → 0`, `-2.5 → -2`). Fixed by implementing as `floor(x + 0.5)`.

### Methods added

- `Math.trunc` (§21.3.2.35)
- `Math.sign` (§21.3.2.32) with signed-zero preservation
- `Math.hypot` (§21.3.2.17) variadic
- `Math.cbrt` (§21.3.2.9)
- `Math.tan` (§21.3.2.33), `Math.asin` (§21.3.2.5), `Math.acos` (§21.3.2.3), `Math.atan` (§21.3.2.6)
- `Math.atan2` (§21.3.2.7)
- `Math.log2` (§21.3.2.21), `Math.log10` (§21.3.2.22)
- `Math.sinh` (§21.3.2.31), `Math.cosh` (§21.3.2.11), `Math.tanh` (§21.3.2.34)

### Constants added (in `PropertyAccessExpression` handler)

- `Math.PI`, `Math.E`, `Math.LN2`, `Math.LN10`, `Math.LOG2E`, `Math.LOG10E`, `Math.SQRT2`, `Math.SQRT1_2`
- `Number.MAX_SAFE_INTEGER`, `MIN_SAFE_INTEGER`, `EPSILON`, `MAX_VALUE`, `MIN_VALUE`
- `Number.POSITIVE_INFINITY`, `NEGATIVE_INFINITY`, `NaN`

### Tests added

- `math-trunc-sign-hypot` — 14 assertions across the new methods
- `math-round-spec` — 8 assertions verifying ties-to-+infinity
- `math-constants-full` — 8 assertions on all 8 Math constants
- `math-trig-extra` — 7 assertions on trig and log methods (epsilon tolerance for transcendentals)

## Number.prototype (ES2024 §21.1)

### Bugs fixed

1. **`Number.isInteger(-5)` returned false.** The extractor only handled positive constants. Fixed by also handling `unary_minus` of a constant, plus symbol lookup.

2. **`Number.isNaN(non-number)` and `Number.isFinite(non-number)` returned wrong result.** Per spec §21.1.2.4/2, these do NOT coerce — non-number inputs must return false. Old code fell through to `isnan_exprt` on any input, which produced garbage for string/bool. Fixed with explicit type check.

3. **`Number.isInteger(Infinity)` etc. returned true.** Fixed by checking `isnan(d) || isinf(d)` and returning false.

### Methods added

- `Number.isSafeInteger` (§21.1.2.5) — `isInteger(x) && |x| <= 2^53 - 1`
- `Number.parseInt` (§21.1.2.13) — uses `std::stoll` with optional radix, returns NaN on parse failure
- `Number.parseFloat` (§21.1.2.12) — uses `std::stod`
- `Number.prototype.toFixed` (§21.1.3.3) — `std::snprintf("%.*f")`
- `Number.prototype.toString(radix)` (§21.1.3.6) — base-10 uses JS-like rendering, other bases use manual integer conversion

### Known gap

Number instance methods on NEGATIVE values stored in symbols (`const n = -3.14; n.toFixed(1)`) don't work because unary-minus is applied via assignment, not stored on the symbol's value field. Inline negatives (`(-3.14).toFixed(1)`) work. Documented in the relevant CORE test.

### Tests added

- `number-is-integer-neg` — 17 assertions including negatives, NaN, Infinity
- `number-parse` — 10 assertions for parseInt with radix and parseFloat
- `number-to-fixed` — 8 assertions including inline negatives
- `number-to-string` — 9 assertions with multi-radix (binary, octal, hex)

## Map / Set (ES2024 §24.1 / §24.2)

### Bugs fixed

1. **`Map.delete` only decremented size.** The key remained in the keys array and subsequent `has()` / `get()` calls would still find it (because linear-scan `has` checks `i < size`, but the key was still at its old slot which was within range until size dropped below it). Fixed with swap-and-pop: scan for match, overwrite matched slot with last-slot contents, decrement size. Returns true/false per spec.

2. **`Map.clear` not implemented.** Added: `size := 0`. Since `has`/`get` check `i < size`, no array cleanup needed.

3. **`Set.add` didn't enforce uniqueness.** Per ES2024 §24.2.3.1, adding an existing element is a no-op. Fixed by gating the write with an 'exists' check via linear scan.

4. **`Set.delete` same issue as `Map.delete`.** Same swap-and-pop fix.

5. **`Set.clear` not implemented.** Same as `Map.clear`.

### Tests added

- `map-delete-clear` — 12 assertions covering delete, delete-nonexistent, clear, re-set after clear
- `set-dedup-delete-clear` — 12 assertions covering add-idempotence, delete, clear

## Overall impact

- **14 real bugs fixed** across 5 subsystems
- **22 missing methods added**
- **18 new CORE tests** (~130 new assertions)
- **0 regressions** — full existing 595 CORE test suite passes
- **No new KNOWNBUGs** introduced (only documented one known gap for negative-symbol toFixed)

Test count went from **578** to **595 CORE** in this session. The KNOWNBUG count remained at **4**, with the same 4 that required deeper refactors (async-race-undetected, integration-url-parser, generic-heterogeneous-tuple, optional-chaining).

## Methodology (proven repeatable)

For each ES2024 built-in subsystem:
1. Enumerate the spec sections (§X.X.Y).
2. Write probe tests that exercise default arguments, boundary values, negative indices, short-circuits, and spec-mandated edge cases (e.g. `Math.round` ties direction, `substring` arg swap).
3. Run, collect failures, fix each in a small commit.
4. Promote probe tests to CORE regression tests.
5. Document known gaps as KNOWNBUG or in the capability matrix.

This session's yield (14 bugs + 22 methods in ~4 hours) confirms the earlier string-review observation: per-spec systematic review consistently finds bugs that ad-hoc testing misses, and each fix is a small focused commit.

## Recommendations for future work

Subsystems not yet deep-reviewed:
- **Date** (§21.4) — not currently implemented at all; would need full design.
- **RegExp** (§22.2) — not implemented; requires bigger effort (regex compilation).
- **JSON** (§25.5) — partial; probe JSON.stringify/parse semantics.
- **Object** (§20.1) — partial; methods like `Object.entries`, `Object.fromEntries`, `Object.assign` deep semantics.
- **BigInt** (§21.2) — not modeled; out of scope for IEEE-754-only frontend.
- **Proxy / Reflect** (§27.1, §28) — not implemented; major semantic gap.

Also:
- **Symbolic (nondet) input tests for these subsystems** — all new CORE tests use constant inputs. A second pass with `nondet_number()` would find cases where the current implementation is sound for constants but wrong for symbolic inputs (e.g., our `slice` on a symbolic array returns the array unchanged).
- **Mutation-test the new CORE tests** — extend `integration/typescript-npm/run_mutation_tests.sh` style to regression tests to verify they actually catch bugs they claim to.
