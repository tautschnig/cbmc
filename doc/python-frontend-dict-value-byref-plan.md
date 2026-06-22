# Python frontend: dict-VALUE-by-reference (mutable values mutated in place)

Status: **PARTIALLY LANDED (2026-06-22).** Option 2 (lvalue value slots)
implemented for **direct (int) keys**: `a[k].append(...)` and
`setdefault(k, default).append(...)` now mutate the stored list/dict in
place, with **0 sweep regressions**. Residuals: string-keyed dicts (kept
read-only — the matched index there depends on a string-solver predicate),
the `v = a[k]; v.append(...)` extraction-aliasing case (same root as the §0
nested-list residual), and empty-`{}` value typing (`dict_setdefault_list`).
Distinct from [§5 dict pass-by-reference](python-frontend-plan.md#dict-byref)
(the *dict itself* as a by-reference parameter — RESOLVED).

---

## Problem (PLR §6.4 + §3.1)

A list/dict stored as a dict value is a mutable object; mutating it in place
must be observed on later reads:

```python
a = {1: []}
a[1].append(5)
assert len(a[1]) == 1        # FAILS today (mutation lost)

a.setdefault(1, []).append(2.0)   # dict_setdefault_list
```

Today dict values are stored **by value** in the `values[]` array, so a
subscript read (`a[k]`) and `setdefault` return a **copy**; the in-place
`append` mutates the copy and is lost. Empirically *all* such mutation is
lost (`a[1].append`, `v=a[1]; v.append`, `setdefault(...).append`).

---

## Feasibility: the by-reference mechanism already works (spike-confirmed)

`convert_dict` already stores a dict value **by reference** when the value
is a *name* resolving to an escaped mutable (`make_python_value(LIST/DICT,
&symbol)` — a `python_value` whose `__list_ptr`/`__class_ptr` points at the
named symbol's storage). With that representation, reads and in-place
mutation propagate correctly and cheaply:

```python
row = []
a = {1: row}
a[1].append(5);  assert len(a[1]) == 1   # SUCCESSFUL (byref1)
row.append(5);   assert len(a[1]) == 1   # SUCCESSFUL (byref2)
v = a[1]; v.append(9); assert len(row)   # SUCCESSFUL (byref3)
```

Crucially this does **not** trigger the string-refinement explosion that
blocks value-keyed dicts (§5): list/dict values use the **opaque**
`__list_ptr`/`__class_ptr` slots, not the refined-string slot. The spike
measured `nondet_dict2` at ~2.1 s and `dict_fromkeys` at 0.05 s — no cliff.

## The gap and the spike

The only missing piece is that list/dict **literal** values (`{1: []}`,
`{k: [1,2]}`) are *not* wrapped by reference — only named escaped values
are. **Spike:** extend `convert_dict`'s value handling so a list/dict
literal value is promoted to a heap symbol referenced by a `python_value`
pointer (the same representation). Result:

- `a={1:[]}; a[1].append(5)`, `setdefault(1,[0]).append(...)`, and
  `v=a[1]; v.append(...)` all **flip to SUCCESSFUL**; perf unaffected.

**But the spike used a per-construction-SITE static counter for the heap
symbol, which is unsound-as-precision for multi-construction:** a dict
literal built repeatedly (a loop, or a function returning `{k: []}` called
twice) shares one heap value, so distinct dicts **alias**:

```python
def make(): return {1: []}
a = make(); b = make()
a[1].append(5); assert len(b[1]) == 0   # wrongly FAILS (a,b alias)
```

This regressed 3 existing tests (`dict-nonprimitive-value-type`,
`nested-container-access`, `nested-container-writes`). The aliasing
produces **false positives** (over-reporting), not false proofs — sound
direction — but it is a net regression, so **the spike was reverted**.

---

## The real requirement: per-instance value identity

The escaped-*name* path works precisely because a named symbol **is** a
per-instance anchor (each `row` is its own storage). Literals and repeated
construction lack such an anchor; a per-site static symbol conflates
instances. A correct implementation needs each runtime dict construction to
own independent value storage. Options:

1. **Per-instance dynamic allocation.** Give the value's heap storage a
   construction-unique symbol (a `__CPROVER` dynamic object, or one
   disambiguated by the enclosing loop/call unwinding index) so each
   unrolled construction is distinct. Sound but needs the allocation to
   participate in symex's per-unwinding renaming (a static symbol does not).
2. **Anchor to the dict's own storage (preferred).** Keep the value
   *inline* in the dict's `values[]` slot, but make subscript-read /
   `setdefault` return the **lvalue slot** (`values[matched_idx]`) instead
   of a copy, so `append` mutates the slot in place. No separate heap
   object, hence **no aliasing** (each dict owns its `values[]`). The
   challenge: the matched index is a runtime key search, so the read must
   yield `values[search_idx]` as an lvalue and list-method dispatch must
   mutate through it.

## Whole-group root (the architectural observation)

This is the **same root** as the nested-mutable-element aliasing in
[§0](python-frontend-plan.md#nested-aliasing): *anonymous* mutable
containers (list elements, dict values, built as literals) need a
**per-instance identity** to be mutated/aliased correctly. Named values
already have it (symbols); literals do not. A general "per-instance identity
for anonymous mutable containers" mechanism would address both the
nested-list-aliasing residual and dict-value-by-reference, rather than two
separate point fixes. Option 2 (return the owning container's lvalue slot,
no separate object) is the most direct expression of this for the dict case.

**Confirmed unified picture (2026-06-22).** The lvalue-slot view splits the
problem cleanly into two cases, identical for lists and dicts:

| pattern | list | dict |
|---|---|---|
| **direct** mutation `c[i].append(...)` | works (subscript already returns the `data[i]` lvalue) | **works now** (subscript/setdefault return the `values[idx]` lvalue, int keys) |
| **extraction** `r = c[i]; r.append(...)` | residual (L2 below) | residual (d3) |

So **direct** nested mutation is solved for both containers via lvalue
slots. The single remaining shared residual is **extraction-then-mutate**:

```python
g = [[0]]; r = g[0]; r.append(5); assert len(g[0]) == 2   # L2: wrongly FAILS
a = {1:[9]}; v = a[1]; v.append(7); assert len(a[1]) == 2  # d3: wrongly FAILS
```

`r = c[i]` copies the slot's *value* into `r`, so the mutation through `r`
is not observed on `c[i]`.

**Investigated 2026-06-22 — slot-aliasing is UNSOUND; the residual is
genuinely blocked on per-object identity.** The tempting fix is
*by-reference-at-extraction*: bind the LHS as a reference to the slot
(`make_python_value(LIST/DICT, &c.<arr>[i])`) so `r` aliases the slot. But
in CPython `r = c[i]` aliases the **object** that `c[i]` currently
references, **not the slot** — and the two diverge whenever the slot's
object identity changes. This is not an edge case; it has **many** hazard
channels, each verified against CPython:

```python
g=[[1]]; r=g[0]; g[0]=[9];      r.append(5)   # subscript-assign: r=[1,5], g[0]=[9]
g=[[1]]; r=g[0]; g.insert(0,x); r.append(5)   # insert shifts: g[1] is the old g[0]
g=[[0],[1]]; r=g[1]; g.pop(0);  r.append(5)   # pop shifts
g=[[2],[1]]; r=g[0]; g.reverse();r.append(5)  # sort/reverse reorder
# … plus rebind (g = ...) and cross-function reassignment f(g): g[i]=...
```

A slot pointer follows the *slot*; CPython's `r` follows the *object*. So
slot-aliasing gives the **wrong** result under any of subscript-assign,
`insert`/`pop`/`sort`/`reverse`/`remove`/`del`, rebind, or a callee that
reassigns `g[i]`. Worse, the divergence can be a **false proof**:

```python
g=[[1]]; r=g[0]; g[0]=[99]; r.append(5)
assert g[0]==[99,5]   # CPython: AssertionError; slot-aliasing would PROVE it
```

(`haz` test — today correctly FAILS; slot-aliasing would wrongly prove it
SUCCESSFUL.) A sound guard would have to exclude **every** channel
(including cross-function reassignment, invisible to a scope scan) — miss
one and it is a false proof, the hard-constraint violation. The benefit is
narrow (the literal extraction pattern; the residual is corpus-invisible),
the downside is maximal, so **slot-aliasing must not be shipped.**

**The only sound fix is per-object identity** — each nested mutable element
is a heap object that both `c[i]` and `r` reference and that moves with the
object (so reorder/reassign behave correctly). That is precisely the
**byref-at-construction** substrate §0 already found **perf-untenable**
(the value-wrapped-string explosion). So the extraction-aliasing residual
(L2 / d3) is confirmed **blocked on the same representation barrier as §0**,
not a missing point fix. It stays a documented, corpus-invisible residual:
the *direct* mutation case (the common one) is solved; *extraction* is left
to the guarded by-value over-approximation (sound).

## Residual sub-problem: empty-dict value typing — RESOLVED (2026-06-22)

`dict_setdefault_list` started with `a = {}` (empty), inferring an `int`
value type so `setdefault(1, [])` mismatched. The empty-container inference
prescan now infers a pending empty dict's key/value types from a
`a.setdefault(k, default).<method>(...)` use (a List default → a list value
type); combined with the int-keyed lvalue-slot setdefault, `dict_setdefault_list`
verifies SUCCESSFUL (DIFF→PASS, 0 sweep regressions).

## Recommendation / phasing

1. **Option 2 (lvalue value slots) — DONE for int keys.** Subscript-read
   and `setdefault` return the owning dict's `values[idx]` lvalue slot;
   `a[k].append(...)` / `setdefault(k, default).append(...)` mutate in
   place. String/value keys stay read-only (the matched index would depend
   on a string-solver predicate). 0 sweep regressions.
2. **Empty-dict value typing — DONE** (setdefault inference); flips
   `dict_setdefault_list`.
3. **Extraction-aliasing (`r = c[i]; mutate r`) — analyzed, BLOCKED.**
   Slot-aliasing is unsound (object-vs-slot divergence, false proofs); the
   sound fix needs per-object identity = the perf-untenable
   byref-at-construction. Stays a sound, guarded, corpus-invisible residual
   for both lists (§0) and dicts (d3).

Net state: **direct** nested mutation works for both containers (int-keyed
dicts); the extraction case is the single shared residual, blocked on the
representation barrier, not on missing plumbing.
