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

---

## Larger fixes (with narrative)

The fixes below are richer than a single table line — each was a model
or analysis-pass change with non-trivial reasoning. They are recorded
in full here so the limitations document can stay focused on
*current* limitations.

### `flow-no-source` / `flow-sanitized` — taint analysis precision (2026-05-28)

**Was**: `custom_bitvector_analysis` (used by `goto-analyzer --taint`)
only tracked taint state on pointer-typed values. TypeScript value-typed
structs (string, array, object) fell back to over-approximation: any
value that could syntactically reach a sink was reported as potentially
tainted, regardless of whether a source actually fed it.

**Fix**: `src/analyses/custom_bitvector_analysis.cpp` was extended to
handle value-typed (non-pointer) operands in three places:
1. `transform()` for `set_may` / `clear_may` / `set_must` / `clear_must`:
   when lhs is non-pointer, set/clear the bit on the identifier directly
   (via `object2id`); for struct-typed lhs, propagate to every recursive
   member.
2. `assign_struct_rec()`: for struct LHS, propagate the parent struct
   identifier's bits in addition to the existing per-member recursion.
3. `eval()` for `get_may` / `get_must`: when src is non-pointer, look
   up bits by identifier directly.

The pointer paths are unchanged, so C-style taint analysis keeps its
existing semantics and tests.

**Regression guards**:
- `regression/typescript-taint/flow-tainted/` — taint detected end-to-end.
- `regression/typescript-taint/flow-no-source/` — no source called → no
  taint (was a false positive before).
- `regression/typescript-taint/flow-sanitized/` — sanitizer clears taint
  (was a false positive before).

### `for-of-method-call-map-write` — `member_exprt` invariant on `String.split` result (2026-05-29)

**Was**: A `for..of` loop whose iterable was a method-call expression
(e.g., `iniData.split("\n")`) combined with `map[k] = map[k] || {}` chain
writes inside the loop tripped the `member_exprt` invariant in
`util/std_expr.h:2862`
(`compound_type_id == ID_struct_tag || ...`).

Minimal repro (11 lines):
```typescript
const f = (iniData: string) => {
  const map: { [k: string]: any } = {};
  for (const k of iniData.split("\n")) {
    map[k] = map[k] || {};
  }
};
```

**Root cause**: `String.prototype.split(non_empty_separator)` on a
non-constant receiver fell through to a generic
`side_effect_expr_nondett{double_type(), ...}` return path. The for-of
conversion then did `member_exprt{arr, "length", signedbv_typet{64}}` on
this `double` expression, violating the precondition.

**Fix**: In `src/typescript/typescript_converter_call.cpp`, the `split`
handler now returns `side_effect_expr_nondett{<typescript_array struct>}`
of the correct shape (over-approximating: each element is an arbitrary
string, length nondet up to `TYPESCRIPT_MAX_ARRAY_LENGTH`). The for-of
conversion now succeeds.

**Regression guard**: `regression/typescript/for-of-method-call-map-write/`
(promoted from KNOWNBUG to CORE on 2026-05-29).

**Found by**: scale run of the CodeQL→CBMC auto-triage pipeline on real
AWS-related TypeScript code (smithy-typescript `parseIni` and an
amplify-cli channel-validation function). Out of 28 unique enclosing
functions tested, 4 hit this invariant — all 4 now parse and analyse
cleanly.

**Commit**: `1dda2c44f6`.

### `regexp-*` — RegExp Phase 2 (metacharacter support via NFA, 2026-05-29)

**Was**: `RegExp.prototype.test(s)` treated the regex source as a
literal substring. `/a.b/.test("axb")` returned `false` because `.` was
matched literally rather than as "any character". Quantifiers (`*`,
`+`, `?`), character classes (`[abc]`, `[^abc]`, `[a-z]`), shorthand
classes (`\d`, `\w`, `\s` and negated forms), and anchors (`^`, `$`)
were all unsupported.

**Implementation**: new module `src/typescript/typescript_regex.{h,cpp}`
implementing a small NFA engine:

1. **Parser**: regex source → AST. Handles `.`, `*`, `+`, `?`, `[...]`
   with negation and ranges, `\d` `\D` `\w` `\W` `\s` `\S` shorthand,
   `^` `$` anchors, and `\\` literal escapes. Returns a parse error
   for unsupported features (`|`, `(...)`, `{n,m}`).
2. **Compiler**: AST → NFA via Thompson's construction. Each
   sub-expression yields a fragment with a single entry state and a
   single dangling exit; quantifier sutures wire fragments via
   epsilon transitions.
3. **Simulator**: NFA × constant input → bool. Subset-construction-
   style step-by-step state-set evolution. ANCHOR_BEGIN /
   ANCHOR_END states are gated on input position via
   `epsilon_close`. RegExp `.test()` partial-match semantics
   implemented by trying every starting offset (unless the pattern
   is `^`-anchored).

**Dispatch**: `typescript_converter_call.cpp`'s `method == "test"`
handler calls `typescript_regex::match(pattern, str)` for constant
pattern + constant string. On `std::nullopt` (unsupported feature),
falls back to the Phase 1 literal-substring fold; on `true`/`false`,
returns the corresponding constant.

**Symbolic input** continues to fall through to the nondet path
(Phase 3, blocked on SMT-string integration).

**Regression guards**:
- `regression/typescript/regexp-metachar-dot/` — `.` metachar.
- `regression/typescript/regexp-quantifiers/` — `*` `+` `?`.
- `regression/typescript/regexp-charclass/` — `[abc]`, `[^abc]`,
  `[a-z]`, multi-range.
- `regression/typescript/regexp-shorthand-classes/` — `\d` `\D`
  `\w` `\W` `\s` `\S`.
- `regression/typescript/regexp-anchors/` — `^`, `$`, combined with
  quantifiers and char classes.

Total of 76 individual `console.assert` checks across the five tests
(plus the existing `regexp-test-literal/` Phase 1 test which
continues to pass — Phase 2 strictly extends Phase 1).
