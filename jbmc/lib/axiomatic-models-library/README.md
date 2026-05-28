# Axiomatic Models Library — opt-in JBMC collections

This jar provides an alternative implementation of
`java.util.HashMap`, `java.util.HashSet`, and
`java.util.LinkedHashMap` for use with JBMC. Compared to the
default `core-models.jar`, these implementations are
**axiomatic** — backed by SMT-LIB array theory rather than
parallel arrays + linear-scan logic.

## Use

Put `axiomatic-models.jar` **before** `core-models.jar` on
the JBMC classpath. The class loader resolves `HashMap` etc.
to whichever appears first; this jar wins.

```bash
jbmc \
  --axiomatic-collections \
  --function MyClass.lemma \
  -cp ./target/classes:axiomatic-models.jar:core-models.jar:cprover-api.jar \
  MyClass
```

The `--axiomatic-collections` CLI flag is currently a marker
(it sets the `axiomatic-collections` option that a future
JBMC lowering pass will consume). The classpath ordering is
what currently selects the implementation.

## What changes

| Method        | core-models.jar       | axiomatic-models.jar     |
|---------------|-----------------------|--------------------------|
| `get(k)`      | `O(size)` linear scan | `O(1)` SMT array select  |
| `put(k, v)`   | `O(size)` linear scan | `O(1)` SMT array store   |
| `containsKey(k)` | `O(size)` linear scan | `O(1)` null-check    |
| `size()`      | `O(1)`                | `O(1)`                   |
| `containsValue(v)` | `O(size)` linear scan | `O(CAPACITY)` linear scan |

The axiomatic versions trade explicit-iteration cost for SMT
array theory, which is much faster for solvers that natively
support it (`--smt2 --z3`, `--cvc5`).

## Limitations

1. **Boxed-primitive keys (Integer, Long, etc.) use
   value-semantic packing**: the lowering pass detects the
   autobox pattern
   `cast(address_of(*<sym>.@Number.@Object), Object*)` and
   reads `(*<sym>).value` (the primitive int / long / etc.
   field) instead of the pointer offset to pack the
   (receiver, key) tuple. This makes
   `m.put(5, v); m.get(5)` round-trip correctly even though
   JBMC's `Integer.valueOf(n)` model returns a fresh
   allocation each call. Recognises Integer, Long, Short,
   Byte, Character, Boolean.

2. **Iteration is supported via skolemizing iterators**:
   `keySet()`, `values()`, `entrySet()`, `Set.iterator()`
   return AxiomaticSetIterator / AxiomaticEntryIterator /
   AxiomaticKeySetView / AxiomaticValuesView / AxiomaticEntrySetView
   instances whose `hasNext()` returns nondet bool and whose
   `next()` returns a fresh nondet element constrained to be
   in the underlying collection (`map.containsKey(key)` /
   `set.contains(elem)`). JBMC's BMC then explores all loop
   iteration counts up to the unwind bound; the loop body
   must hold for every consistent element.

   This is sound but expensive: each iteration introduces a
   nondet key constrained by an axiomatic-encoded
   `containsKey` lookup, which the SAT solver expands to one
   array select per iteration. Lemmas with multiple nested
   iterations (e.g. FormulaSpec.satUnsatExclusive) often
   exceed JBMC's time budget at corpus-default unwind=10.

3. **`size()` is over-approximate for fresh allocations**:
   `m = new HashMap<>(); m.put(k, v)` increments
   `_sz[receiver]` only if `_kv[pack(receiver, k)]` was
   `null` before the put. The global `_kv` is NOT initialised
   at `__CPROVER_initialize` (deliberately, so that input
   parameter Maps can have nondet contents), so for a fresh
   allocation the slot's prior value is nondet and the
   increment may or may not happen. As a result `m.size()`
   after a sequence of puts is bounded above by the put count
   but not necessarily equal to it. Lemmas relying on
   `size()` exactness are not supported.

4. **Hash collisions**: keys with `k1.hashCode() == k2.hashCode() (mod CAPACITY)`
   alias to the same slot. The model silently overwrites. For
   `Integer` keys in `[0, CAPACITY)`, no collisions occur —
   `Integer.hashCode()` is the identity, and `slot = k & MASK`
   is injective on this range. For larger or nondet-unbounded
   `Integer` keys, CBMC's nondet exploration WILL find
   collision counterexamples that are not real bugs.

   **Mitigation**: add explicit `CProver.assume(k < CAPACITY)`
   in the lemma's precondition for Map-keyed nondet inputs,
   or accept the model's domain restriction.

4. **Null values are unset**: storing `null` as a value is
   indistinguishable from "no entry". Lemmas that legitimately
   use `null` values must NOT enable
   `--axiomatic-collections`.

5. **Iteration order unspecified**: `LinkedHashMap` does NOT
   preserve insertion order; `keySet()`, `values()`,
   `entrySet()` throw rather than return a view.

6. **`equals` / `hashCode` opaque**: marked as
   `CProver.notModelled`. Lemmas relying on these fall back to
   nondet.

## Capacity

`CAPACITY = 1024` is chosen as a power of two large enough
that small-`Integer` keys never collide and small enough that
`containsValue`'s linear scan remains tractable. Raising
`CAPACITY` does NOT cost CBMC unwind iterations — the array
is symbolic and CBMC's array theory handles it directly.

## Future work

The flag `--axiomatic-collections` is reserved for a future
JBMC lowering pass that would replace `HashMap.get(k)` calls
directly with `index_exprt(receiver.kv, k)`, eliminating the
hash-mod indirection and the collision limitation entirely.
The pure-Java encoding here is the prototype.
