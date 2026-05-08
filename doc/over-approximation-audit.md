# TypeScript Frontend Over-Approximation Audit

Date: 2026-05-08  
Status: in progress

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

## Resolved (previously documented as nondet, now precise)

### ✅ Math.floor / ceil / trunc / round on symbolic input
- **Was**: fell through to `side_effect_expr_nondett{double_type()}`
- **Now**: `floatbv_round_to_integral_exprt{x, mode}` with FE_DOWNWARD / FE_UPWARD / FE_TOWARDZERO / FE_TONEAREST
- **C frontend reference**: `src/ansi-c/library/math.c:1290` (`floor()`)
- **CBMC primitive**: `src/util/floatbv_expr.h` (`floatbv_round_to_integral_exprt`)

### ✅ Number.isInteger / isSafeInteger on symbolic floats
- **Was**: fell through to nondet; documented as requiring `--ts-integer-mode` for symbolic
- **Now**: `!isnan(x) && !isinf(x) && round_to_integral(x, TOWARDZERO) == x`
- **CBMC primitives**: `isnan_exprt`, `isinf_exprt`, `ieee_float_equal_exprt`, `floatbv_round_to_integral_exprt`

### ✅ Math.abs / sign / max / min on symbolic
- **Was**: only abs had symbolic encoding; sign/max/min fell through
- **Now**: `if_exprt` chains over comparisons
- **Status**: built-in CBMC expressions

## Remaining to investigate

### Array / collection operations

1. **Array.indexOf with symbolic target** → nondet
2. **Array.slice with symbolic start/end** → nondet (returns original)
3. **Array.splice with symbolic args** → nondet (returns unchanged)
4. **Array.fill with symbolic args** → nondet fallback
5. **Array.sort with non-constant array or unrecognized comparator** → unchanged

### String operations with symbolic non-receiver args

1. **String.indexOf with symbolic fromIndex** → nondet
2. **String.substring/slice with symbolic indices** → nondet
3. **String.repeat with symbolic count** → nondet
4. **String.padStart/padEnd with symbolic target length** → nondet
5. **String.includes/startsWith/endsWith with symbolic needle** → nondet
6. **String.charAt / charCodeAt with symbolic index** → nondet
7. **String concatenation with parameter-typed strings** → correct length, nondet data

### Conversions

1. **String-to-number (`+"42"`)** → not implemented
2. **Symbolic number to string (`(x).toString()` with symbolic x)** → nondet
3. **Symbolic string to number (`Number("42")` with symbolic string)** → nondet

### Other

1. **Map/Set iteration (forEach, keys, values, entries)** → unknown / nondet
2. **Error subclass detection** (`e instanceof TypeError`) → limited
3. **Optional chaining on inline nested types** → currently limited

## Next steps

For each remaining item, I will:
- Identify the CBMC primitive (if any) that encodes the semantics
- Check the C frontend's encoding
- Implement a precise version or document the true limitation
## Resolved (additional)

### ✅ Array.indexOf / lastIndexOf with symbolic target
- **Was**: nondet fallback
- **Now**: nested `if_exprt` chain scanning array data (same pattern as Map.has)

### ✅ Array.fill with symbolic start/end
- **Was**: fell back to default bounds, ignoring symbolic args
- **Now**: per-slot `if_exprt{in_range, fill_val, orig}` where `in_range` uses symbolic comparisons

### ✅ Array.slice with symbolic start/end
- **Was**: nondet / returned original unchanged
- **Now**: emits symbolic `result_len = max(0, clamp(end) - clamp(start))` and per-slot `if_exprt` chain over all possible `start` values matching the source slot
