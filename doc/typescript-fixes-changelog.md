# TypeScript Frontend — Fixes Changelog

Historical record of bugs found and fixed during spec cross-referencing,
soundness reviews, and property-based fuzzing. Each entry cites the CORE
regression test that guards against regression and the commit that fixed it.

Moved from `doc/typescript-capability-matrix.md` to keep the matrix
focused on current capability status rather than historical fixes.

---

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
| `spec-string-plus-coerce` | `"pi: " + 3.14` rendered as `"pi: 3"` (round_to_integral used as statement) | e93f234c53 |
| `spec-array-concat-variadic` | `[1,2].concat(3, 4)` produced length 1 (only first arg considered) | e93f234c53 |
| `spec-array-includes-fromindex` | `Array.includes` ignored fromIndex | e93f234c53 |
| `spec-array-join-empty` | `[].join(",")` fell through to symbolic fallback | e93f234c53 |
| `spec-arrow-funcptr-live` | `const f = foo; f()` didn't dispatch (typecast instead of address_of + pre-scan miss) | e93f234c53 |
| `spec-for-loop-increment-continue` | for-loop incrementor was a dead expression; `continue` skipped it | e93f234c53 |
| `spec-logical-operand-value` | `&&` / `\|\|` returned boolean instead of an operand value (§13.13) | e93f234c53 |
| `spec2-math-hypot-zero` | `Math.hypot()` with zero args returned nondet | 0f86b8adeb |
| `spec2-math-imul` | Math.imul missing; precision bug on large args | 0f86b8adeb |
| `spec2-math-clz32` | Math.clz32 missing | 0f86b8adeb |
| `spec2-math-log1p-expm1` | Math.log1p and Math.expm1 missing | 0f86b8adeb |
| `spec2-toboolean-string` | ToBoolean on strings produced nondet (no struct→bool defined) | 0f86b8adeb |
| `spec2-parseint-radix` | parseInt had a placeholder returning 0 for every input | 0f86b8adeb |
| `spec2-destructure-defaults-short` | Destructure defaults didn't apply when source was too short/empty | 0f86b8adeb |
| `spec2-forEach` | Array.prototype.forEach not implemented | 0f86b8adeb |
| `spec2-number-coerce-empty` | Number("") returned 0 only via placeholder — not actually parsed | 0f86b8adeb |
| `spec3-bitwise-shift-mask` | Shift count not masked to low 5 bits (1<<32 produced 0 instead of 1) | 66de725c1b |
| `spec3-unsigned-shift` | `>>>` returned signed result (-1>>>0 was -1 instead of 4294967295) | 66de725c1b |
| `spec3-sqrt-negative-nan` | `Math.sqrt(-1)` returned nondet instead of NaN | 66de725c1b |
| `spec3-array-includes-nan` | `[NaN].includes(NaN)` returned false (used IEEE === instead of SameValueZero) | 66de725c1b |
| `spec3-in-operator-array` | `i in arr` returned nondet (string-keyed-only, plus type-promotion bug) | 66de725c1b |
| `spec3-string-utf8-bmp` | Multi-byte UTF-8 characters counted as multiple length units | 66de725c1b |
| `spec3-string-utf16-astral` | Astral characters not encoded as UTF-16 surrogate pairs | 66de725c1b |
| `spec3-string-coerce-array` | `"" + arr` didn't call array's toString (join with ",") | 66de725c1b |
| `spec3-string-relational` | `<`/`>`/`<=`/`>=` on strings returned undefined struct compare | 66de725c1b |
