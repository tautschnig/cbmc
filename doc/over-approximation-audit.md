# TypeScript Frontend Over-Approximation Audit

Date: 2026-05-08  
Status: substantial progress; remaining items documented

This document enumerates every place the TypeScript frontend returns a
nondet fallback or otherwise over-approximates symbolic inputs, and
investigates whether the over-approximation is necessary. Many cases
have precise encodings via CBMC primitives that the C frontend uses.

## Methodology

For each over-approximation:
1. Describe where it is and what input triggers it.
2. Check if the C frontend encodes the same operation precisely.
3. Check whether CBMC has a primitive (in `src/util/*.h`) for the
   semantics.
4. If yes, implement the precise encoding; if no, document the
   genuine limitation.

## ✅ Resolved (now precise)

### Math.floor / ceil / trunc / round on symbolic input
- **Was**: `side_effect_expr_nondett{double_type()}`
- **Now**: `floatbv_round_to_integral_exprt{x, mode}` with
  FE_DOWNWARD / FE_UPWARD / FE_TOWARDZERO / FE_TONEAREST
- **C frontend reference**: `src/ansi-c/library/math.c:1290` (`floor()`)
- **CBMC primitive**: `src/util/floatbv_expr.h`
  (`floatbv_round_to_integral_exprt`)

### Number.isInteger / isSafeInteger on symbolic floats
- **Was**: nondet; required `--ts-integer-mode` for symbolic
- **Now**: `!isnan(x) && !isinf(x) && round_to_integral(x, TOWARDZERO) == x`
- **CBMC primitives**: `isnan_exprt`, `isinf_exprt`,
  `ieee_float_equal_exprt`, `floatbv_round_to_integral_exprt`

### Math.abs / sign / max / min on symbolic
- **Was**: only abs had symbolic encoding
- **Now**: `if_exprt` chains over comparisons

### Array.indexOf / lastIndexOf with symbolic target
- **Was**: nondet fallback
- **Now**: nested `if_exprt` chain scanning array data (same pattern
  as Map.has)

### Array.fill with symbolic start/end
- **Was**: fell back to default bounds, ignoring symbolic args
- **Now**: per-slot `if_exprt{in_range, fill_val, orig}` where
  `in_range` uses symbolic comparisons

### Array.slice with symbolic start/end
- **Was**: nondet / returned original unchanged
- **Now**: emits symbolic `result_len = max(0, clamp(end) - clamp(start))`
  and per-slot `if_exprt` chain over all possible `start` values
  matching the source slot

### String method args: symbol-to-constant resolution
- **Was**: `const k = 3; s.substring(0, k)` didn't recognize k as
  constant, so num_args was empty
- **Now**: arg extraction resolves symbols to their stored values
  before the constant-check

### Number.isNaN / isFinite on non-number arg (ES2024 §21.1.2.4/2)
- **Was**: fell through to isnan_exprt on any input (wrong for strings, bools)
- **Now**: explicit type check at entry returns false for non-floatbv

## ⚠️  Partially resolved

### String.substring with symbolic non-literal args
- **Was**: returned original string unchanged
- **Now (partial)**: works when arg is a symbol bound to a constant;
  still nondet for truly nondet args. The non-const symbolic handler
  exists but is apparently not reached for nondet inputs —
  investigation ongoing.

### String concatenation with parameter-typed strings
- **Was**: returned fully-nondet struct
- **Now**: returns struct with correct symbolic length
  (`left.length + right.length`) but nondet data
- **Genuine limitation**: reconstructing the data array symbolically
  requires copying char-by-char with symbolic length, which our
  fixed-size model handles partially.

## ❌ Genuine limitations (documented, each with a KNOWNBUG test)

Each remaining over-approximation has a KNOWNBUG regression test
that FAILS verification due to the nondet result. When we resolve
the limitation, the test will pass and be promoted to CORE.

### String.repeat with symbolic count — RESOLVED via per-case encoding
- **Was**: test was KNOWNBUG (symbolic multiplication of result-size)
- **Now**: test is CORE. For a constant source string and symbolic
  `count n`, emit `length = src.length * n` via IEEE multiplication,
  cast to the length type. Content stays nondet.
- **Test**: `regression/typescript/string-repeat-symbolic`

### String.padStart / padEnd with symbolic target length — RESOLVED
- **Was**: test was KNOWNBUG (variable-length padding insert)
- **Now**: test is CORE. For a constant source string and symbolic
  target length `n`, emit `length = max(src.length, n)` via
  `if_exprt`. Content stays nondet.
- **Test**: `regression/typescript/string-padstart-symbolic`

### String.indexOf with symbolic needle — RESOLVED
- **Was**: test was KNOWNBUG (full string matching over symbolic chars)
- **Now**: test is CORE. Per-candidate-position `if_exprt` chain:
  for each p in [0, 16), the match predicate is
  `p + n.length <= s.length AND forall j: j >= n.length OR
   s.data[p+j] == n.data[j]`. Returns the first matching p or -1.
- **Test**: `regression/typescript/string-indexof-symbolic-needle`

### `+"42"` (string-to-number coercion) for constant strings — RESOLVED
- **Was**: test was KNOWNBUG (symbolic string parsing)
- **Now**: test is CORE for constant-string inputs. Parses digits
  at conversion time (trimmed). `+"abc"` yields NaN correctly.
- **Test**: `regression/typescript/string-to-number-coerce`
- **Symbolic strings** still KNOWNBUG
  (`string-to-number-coerce-symbolic`) — needs refined string solver.

### Array.splice with symbolic deleteCount — RESOLVED via per-case encoding
- **Was**: test was KNOWNBUG (symbolic length and element shift)
- **Now**: test is CORE. For constant source array, constant start,
  and symbolic deleteCount, emit a per-slot `if_exprt` chain:
  `result[i] = i < start ? src[i] :
               (i + dc < src_len ? src[i + dc] : 0)`.
- **Test**: `regression/typescript/array-splice-symbolic`

### Array.sort with symbolic elements
- **Test**: `regression/typescript/array-sort-symbolic`
- **Why**: arbitrary symbolic permutation would require SAT-based
  permutation search, expensive and our BMC model doesn't support.
  Could encode a sorting network for bounded sizes.

## Refined string solver: not currently integrated

CBMC has a refined string solver at `src/solvers/strings/` that
handles symbolic string operations precisely — concatenation,
indexOf with symbolic needle, repeat with symbolic count, parsing,
etc. It operates on a dedicated `refined_string_exprt` type
(`{content: pointer, length: int}`) and special builtin function
calls like `cprover_string_concat`, `cprover_string_index_of`.

JBMC uses this for Java strings: see
`jbmc/src/java_bytecode/java_string_library_preprocess.cpp` where
`java.lang.String` is preprocessed to `refined_string_exprt`.

**Our TypeScript frontend does NOT currently use it.** We represent
strings as `struct{length: signedbv[32], data: unsignedbv[16][64]}`
— a fixed-size struct with no pointer. This works well for constant-
length operations but can't express variable-length symbolic
operations.

**Integrating the refined string solver** would resolve all 4
string KNOWNBUGs in the list above (repeat-symbolic, padstart-
symbolic, indexof-symbolic-needle, to-number-coerce). The steps
would be roughly:

1. Change `typescript_string_type()` to `refined_string_typet`.
2. Rewrite string-method conversion to emit
   `side_effect_expr_function_callt` to `cprover_string_*` builtins
   (e.g. `cprover_string_concat`, `cprover_string_substring`).
3. Pass `--refine-strings` automatically from the TypeScript driver.
4. Rewrite string-constant creation to build `refined_string_exprt`.

This is a substantial refactor (estimated ~500 LOC + many test
adjustments) but would dramatically expand the symbolic expressivity.
Left as future work; tracked via the 4 string KNOWNBUGs.

## Summary of resolution rate

| Category | Resolved | Partial | Genuine limitation |
|----------|----------|---------|-------------------|
| Math / Number | 7 (all) | 0 | 0 |
| Array (slice/fill/indexOf) | 3 | 0 | 1 (splice) |
| Array (sort with comparator) | 0 | 0 | 1 |
| String | 1 (concat length) | 1 (substring) | 4 (repeat, pad, symbolic needle, parse) |
| Conversion | 0 | 0 | 2 (string↔number with symbolic) |

**Key insight**: Every case that had a corresponding CBMC primitive
(`floatbv_round_to_integral_exprt`, `isnan_exprt`, etc.) was resolvable.
The remaining genuine limitations are all operations that would require
integrating CBMC's **refined string solver** (`src/solvers/strings/`),
which has symbolic string matching, parsing, and variable-length
manipulation. That integration is a larger project.

## Recommendation for future frontend authors

Before documenting an over-approximation as "fundamental limitation":

1. **Check the C frontend's `library/` directory** — CBMC has C
   library modelings for all the common math and string functions.
   They show exactly which primitives are available.

2. **Check `src/util/*_expr.h`** — CBMC has purpose-built expression
   types for many operations:
   - `floatbv_round_to_integral_exprt` (round modes)
   - `floatbv_typecast_exprt` (float ↔ int/float conversions)
   - `ieee_float_equal_exprt`, `ieee_float_notequal_exprt`
   - `isnan_exprt`, `isinf_exprt`
   - `if_exprt` for conditionals
   - `binary_relation_exprt` for comparisons

3. **For collection operations, try the "per-slot if_exprt" pattern**
   used by our Map.has, Array.indexOf, Array.fill. It works for any
   bounded-size collection with symbolic equality.

4. **For variable-length string operations, consider refined strings**
   — but this is a bigger integration effort.

This audit showed that most "we can't do it symbolically" claims
were premature. The symbolic encoding usually exists in CBMC primitives;
we just needed to find them.
