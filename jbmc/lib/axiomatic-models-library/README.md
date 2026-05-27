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

1. **Hash collisions**: keys with `k1.hashCode() == k2.hashCode() (mod CAPACITY)`
   alias to the same slot. The model silently overwrites. For
   `Integer` keys in `[0, CAPACITY)`, no collisions occur —
   `Integer.hashCode()` is the identity, and `slot = k & MASK`
   is injective on this range. For larger or nondet-unbounded
   `Integer` keys, CBMC's nondet exploration WILL find
   collision counterexamples that are not real bugs.

   **Mitigation**: add explicit `CProver.assume(k < CAPACITY)`
   in the lemma's precondition for Map-keyed nondet inputs,
   or accept the model's domain restriction.

2. **Null values are unset**: storing `null` as a value is
   indistinguishable from "no entry". Lemmas that legitimately
   use `null` values must NOT enable
   `--axiomatic-collections`.

3. **Iteration order unspecified**: `LinkedHashMap` does NOT
   preserve insertion order; `keySet()`, `values()`,
   `entrySet()` return opaque nondet views.

4. **`equals` / `hashCode` opaque**: marked as
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
