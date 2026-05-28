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

1. **Boxed-primitive keys (Integer, Long, etc.) are unsound**:
   the lowering pass uses pointer-identity to pack the
   (receiver, key) tuple into a 64-bit array index. Java's
   `Integer.valueOf(n)` is modelled by JBMC's core-models
   library as `return new Integer(n)` (no cache), so two calls
   with the same int value produce DIFFERENT pointers. The
   axiomatic encoding sees them as DIFFERENT keys, breaking
   `m.put(k, v); m.get(k)` round-trips when the boxing happens
   at distinct call sites. Lemmas with `Map<Integer, ...>` or
   `Set<Integer>` will produce spurious counterexamples.

   **Future work**: special-case boxed-primitive keys in the
   lowering pass — read `key.@Number.value` instead of using
   `pointer_offset(key)` to pack the index. This requires
   detecting the static class of the key argument at goto time.

2. **Iteration is not supported**: `keySet()`, `values()`,
   `entrySet()`, `Set.iterator()` all `throw new
   UnsupportedOperationException(...)` rather than returning
   a stub. Returning null silently kills paths via JBMC's
   auto-injected `ASSERT(it != null); ASSUME(it != null)`
   pattern around `hasNext()`, which would prove iterating
   lemmas vacuously (see VacuityTest in
   /tmp/axiomatic-test/).

3. **Hash collisions**: keys with `k1.hashCode() == k2.hashCode() (mod CAPACITY)`
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
