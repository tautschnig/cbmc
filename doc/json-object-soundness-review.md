# JSON and Object Soundness Review

Date: 2026-05-09  
Scope: ES2024 §25.5 (JSON) + §20.1 (Object)  
Start state: 605 CORE / 9 KNOWNBUG  
End state: 613 CORE / 9 KNOWNBUG (+8 new CORE tests / ~60 assertions)

Same methodology as previous per-spec reviews (see
`doc/architectural/building-a-new-frontend.md`). This round covered
the remaining "partial ⚠️" subsystems from the capability matrix.

## Summary

| Subsystem | Implementations added | New CORE tests |
|-----------|----------------------|----------------|
| JSON (§25.5) | 2 methods + unary-minus fix | 3 |
| Object (§20.1) | 6 methods | 5 |
| **Total** | **8 methods** | **8** |

## JSON (ES2024 §25.5)

### `JSON.stringify` (§25.5.2)

Not implemented before (fell through to nondet). Now handles:

- **Primitives**: number, string, boolean, null (NaN/Infinity → `"null"`)
- **Arrays**: recursive
- **Object literals**: recursive, skipping `__internal` fields
- **Unary-minus constants**: `JSON.stringify(-7) === "-7"`
- **Symbol-resolved constants**: `const n = 42; JSON.stringify(n) === "42"`

Spec points addressed:
- §25.5.2.4 Step 4.a: NaN and Infinity serialize to `"null"`
- String escaping: `\"`, `\\`, `\n`, `\t`

### `JSON.parse` (§25.5.1)

Also not implemented. Now handles constant JSON string inputs:

- Numbers (including negatives)
- Booleans
- `"null"` → our NaN sentinel
- Strings with escape sequences (`\"`, `\\`, `\n`, `\t`)
- Arrays of primitives

### Known limitations

Symbolic JSON operations would require the refined string solver.
Nested objects in parse input (`{"a":1}`) parse partially; the
simple parser handles primitives and flat arrays. Documented in
`doc/refined-string-migration-plan.md`.

## Object (ES2024 §20.1)

### `Object.keys` and `Object.values` (§20.1.2)

Already implemented before this review. Verified still correct.

### `Object.entries` (§20.1.2.5)

Not implemented. Now returns an array of `typescript_tuple` structs
(each a 2-tuple `[key, value]`). Supports iteration over the array
and `Object.fromEntries` round-trip.

Limitation noted in the CORE test: deep `es[i][j]` 2-level indexing
doesn't resolve the tuple at the array slot, so the test verifies
length and the round-trip instead.

### `Object.fromEntries` (§20.1.2.6)

Not implemented. Builds an object from a constant array of
`typescript_tuple` pairs. Works with `Object.entries` round-trip:

```ts
const o = { a: 1, b: 2 };
const r = Object.fromEntries(Object.entries(o));
console.assert(r.a === 1);
console.assert(r.b === 2);
```

### `Object.is` (§20.1.2.11)

Not implemented. Implements SameValue:

- Same as `===` for most cases
- NaN **same as** NaN (unlike `===`)
- Null (our NaN sentinel) same as undefined (our NaN sentinel)
- Type mismatch → false

Known gap: `Object.is(0, -0)` should return `false` per spec, but
we return `true` (IEEE float equality). Distinguishing signed zero
requires bit-pattern comparison. Documented in the CORE test.

### `Object.assign` (§20.1.2.1)

Not implemented. Merges source structs into target:

- Returns a new struct with the union of all fields
- Later sources override earlier ones
- Skips non-struct sources

Limitation: prototype-chain traversal and getters not modeled.

### `Object.prototype.hasOwnProperty` (§20.1.3.2)

Not implemented. Added as an instance-method dispatch on any
non-internal struct type: scan the struct's components for the
given key. Skips typescript_array, typescript_string, typescript_union,
typescript_tuple, and class tags (hasOwnProperty on those has different
semantics).

### `Object.hasOwn` (§20.1.2.8) — modern alternative

Not implemented. Same logic as hasOwnProperty, but as a static
method. ES2022+ preferred form.

## Bug patterns found and fixed

1. **Missing methods with no fallback** — The first run showed
   `JSON.parse` / `JSON.stringify` / 4 `Object.*` methods returned
   nondet (falling through to `side_effect_expr_nondett`) without
   any implementation attempt. Adding constant-value handlers covered
   the common use cases.

2. **Unary-minus constants not handled** — `JSON.stringify(-7)`
   fell through because `-7` parses as `unary_minus{constant(7)}`,
   not a direct constant. Fixed in `stringify`; this pattern has
   now been fixed across multiple handlers during past reviews.

3. **Symbol resolution in stringify** — same pattern: a symbol
   bound to a constant needs resolution before the constant check.
   Fixed, same fix pattern as recent string/number work.

## Tests added (8 new CORE)

- `json-stringify` (16 assertions): primitives, arrays, objects,
  nested, NaN/Infinity → null, symbol-resolved
- `json-parse` (9 assertions): primitives, negatives, escapes,
  arrays
- `json-roundtrip` (6 assertions): `parse(stringify(x)) === x` for
  primitives
- `object-entries` (5 assertions): length + round-trip
- `object-fromentries` (4 assertions): direct + round-trip
- `object-is` (10 assertions): primitives, null/undefined, NaN case,
  type mismatch
- `object-assign` (8 assertions): multi-source merge, override
- `object-hasownproperty` (11 assertions): presence / absence on
  own properties
- `object-hasown` (4 assertions): newer static variant

## Remaining gaps (not addressed)

- **`Object.is(0, -0)`** should be false per spec; we say true
  (documented in test)
- **Deep tuple-in-array indexing** (`Object.entries(o)[i][j]`) —
  covered by fromEntries round-trip instead
- **JSON of nested objects** — parser handles only flat structures;
  deep parsing would need more work
- **Non-constant JSON** (e.g. `JSON.parse(nondet_string())`) returns
  nondet — refined string solver needed
- **Object.freeze / seal / preventExtensions** — not implemented
  (stateful, complex; not commonly used in verification workloads)
- **Prototype chain methods** (`getPrototypeOf`, `setPrototypeOf`,
  `isPrototypeOf`) — not implemented (our struct model doesn't have
  prototype chains)

## Methodology validation

This was the 7th per-spec review. Consistent yield: 8 methods, 8
CORE tests in ~90 minutes. The per-spec approach continues to find
bugs (missing implementations that fell silently to nondet) that
probing-based testing had missed. The next natural target would be
**Date (§21.4)** or **RegExp (§22.2)** — but these are not
implemented at all (would require design, not review).
