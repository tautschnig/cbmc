# Python frontend: dict pass-by-reference & the value-string storage question

Status: **design / decision doc**. No global representation change has been
made. This records where the dict-by-reference work landed, *why* the
"obvious" next step (a uniform `dict[value, value]` default) is blocked, and
the options for arriving at a sound, performant end state.

Last updated: 2026-06-08.

---

## TL;DR

- List method parameters are passed **by reference** with sound element
  conversion (PLR §3.1). Class-instance parameters too. dict parameters are
  still passed **by value**, which silently drops mutations made through the
  parameter — a latent unsoundness (false proofs).
- The clean way to fix dict-by-reference is to give a bare `dict` parameter a
  *uniform* element type so any concrete dict argument can be promoted to it,
  exactly as lists do (`list[value]`). The natural choice is
  `dict[value, value]`.
- The read/membership machinery for `dict[value, value]` is now **sound** (see
  `value_equal`, landed). But flipping the bare-`dict` **default** to
  `dict[value, value]` causes a severe **performance** regression that is
  **structural**, not a missing fast path: `python_value` stores strings
  *behind a pointer*, so value-keyed *string* dicts force the string-refinement
  solver to reason about many pointer-indirected strings at once and it
  explodes.
- Recommendation: do **not** flip the global default. Pursue **Option B**
  (targeted by-reference for string-keyed dicts, keeping the `dict[str, value]`
  default) as the near-term sound win, and treat the `python_value`
  string-storage change (**Option A**) as a separate, deliberately-scoped
  project that would unblock by-reference for *non*-string-keyed dicts.

---

## Background: what we want and why it is blocked

`safe_typecast`'s struct→pointer boundary is the single shared mechanism that
makes container parameters pass by reference (it materialises rvalues, promotes
element types, and copies mutations back; the free-function and method call
paths both funnel through it). For lists this is sound: a bare `list` parameter
is `list[value]`, so any concrete `list[int]` / `list[str]` argument promotes by
wrapping each element into the tagged union, and mutations copy back.

dict does **not** get this treatment. A bare `dict` parameter is
`dict[str, value]` (string keys, tagged-union values). A concrete argument such
as `dict[int, int]` cannot be promoted to it: `str` vs `int` keys is **not** a
`python_value` widening, and a raw pointer reinterpret mis-reads the key array.
So dict parameters remain by value and mutations through them are dropped.

The "make it uniform like lists" fix is to default a bare `dict` to
`dict[value, value]`. Then every concrete dict argument promotes (wrap each key
and value into the tagged union), mirroring the list path.

---

## The representation today

```
python_value  (tag-python_value), see src/python/python_value_type.h
  __tag       : int32          // NONE=0 INT=1 FLOAT=2 BOOL=3 STR=4 LIST=5 ...
  __int_val   : int64
  __float_val : double
  __bool_val  : int32
  __str_ptr   : python_string *      // <-- string stored BEHIND a pointer
  __list_ptr  : void *
  __class_ptr : void *

python_string (tag-__CPROVER_refined_string_type), see src/python/python_types.h
  length      : int64
  data        : uint8 *              // CBMC refined-string char array
```

Key facts:

- `python_string` is CBMC's **refined string type** — the string-refinement
  solver recognises this tag and reasons about it natively.
- A `python_value` holding a string is therefore a **double indirection**:
  `value.__str_ptr -> python_string -> .data -> chars`.
- `__list_ptr` / `__class_ptr` *must* be opaque pointers (their pointees are
  self-referential, which `smt2_conv` would otherwise emit as forward-referenced
  `declare-datatypes`, breaking cvc5). `__str_ptr` does **not** have that
  excuse — `python_string` is not self-referential — so an inline string field
  is representable in principle.

Containers:

```
python_list[T]            : { int64 length; T data[N]; }
python_dict_type(K, V)    : { int64 length; K keys[N]; V values[N]; }   // N=16
```

A `dict[value, value]` thus has a `keys[16]` array of `python_value`; if those
keys are strings, that is 16 `__str_ptr` pointers into 16 separate
`python_string` objects.

---

## Diagnosis: why `dict[value, value]` is slow

Measured on `esbmc.git/regression/python/nondet_dict2` (a string-keyed dict
passed to a function, mutated, returned, read back) and minimal repros, with
`--unwind 10`, `ulimit -v 8388608`:

| Configuration | Result |
|---|---|
| bare dict = `dict[str, value]` (current default) | **VERIFICATION SUCCESSFUL, 0.45 s** |
| bare dict = `dict[value, value]` | **TIMEOUT (> 120 s)** |
| `dict[value, value]`, `value_equal` stubbed to `false` | still TIMEOUT |
| `dict[value, value]`, `value_equal` returns immediately | still TIMEOUT |
| `dict[value, value]` through a function, **int** keys | **0.42 s** |
| `dict[value, value]` through a function, **str** keys | **TIMEOUT** |

Stage breakdown for the timing-out case: **Symex 1.2 s, 6 VCCs (3 after
simplification)** — symbolic execution is fast. The hang is entirely in the
**solver** ("string refinement loop with MiniSAT").

Conclusions from the table:

1. The key-comparison logic (`value_equal`) is **not** the cost — stubbing it
   out does not help.
2. The cost is **specific to string keys** — int-keyed `dict[value, value]` is
   fast.
3. Direct (inline) string keys are fine — the old `dict[str, value]` default
   copies a string-keyed dict by value in 0.45 s.

So the explosion is triggered precisely by **value-wrapped (pointer-indirected)
strings in bulk**: copying a `dict[value, value]` (by-value parameter + return)
and reading it forces the string-refinement loop to resolve 16 `__str_ptr`
pointers and reason about all the pointed-to refined strings together. The
double indirection plus pointer-aliasing reasoning is what blows up; the same
strings stored *inline* (as in `dict[str, value]` keys) are cheap.

The constant-key fast path is **not** the lever here: in `nondet_dict2` the read
target is a function return, not a tracked literal, so the access is symbolic
regardless.

---

## Options

### Option A — inline the refined string into `python_value` (representation change)

Replace `__str_ptr : python_string *` with an inline `__str : python_string`
field (removing one indirection level; the refined string's own `.data` pointer
stays, as it must). The hypothesis is that removing the `__str_ptr` aliasing
layer lets the string-refinement loop treat value-wrapped strings like inline
ones, restoring `dict[str, value]`-class performance and unblocking the uniform
`dict[value, value]` default — and therefore by-reference for *all* dicts.

- Pros: addresses the root cause; would speed up *any* heavy value-wrapped-string
  workload, not just dicts; makes the uniform-default path viable, which in turn
  closes dict-by-reference soundly via the already-shared `safe_typecast`
  boundary.
- Cons / risk: **large blast radius**. `python_value` is the single most
  pervasive type in the frontend — every `wrap_value` / `unwrap_value`, every
  tagged-union compare/truthiness site, `make_python_value`, and the SMT
  encoding depend on its layout. Changing its size/layout risks broad
  regressions and must be validated against all three suites + the full ESBMC
  sweep. Also unverified: whether inlining actually tames the string-refinement
  loop (the refined string still carries a `data` pointer). **Must be
  prototyped behind a measurement before committing.**
- First step: a spike that changes only the struct definition + `make_python_value`
  / `unwrap_value` / `value_equal` str access, then re-runs the diagnosis table
  above. Proceed only if `strfn`-style repros drop back to sub-second.

### Option B — targeted by-reference for string-keyed dicts (no global change) — RECOMMENDED near-term

Keep the bare-`dict` default at `dict[str, value]`. Enable dict pass-by-reference
**only when the argument's keys already match the parameter's key type** (the
common JSON / kwargs case: `dict[str, int]` → `dict[str, value]`). Then only the
**values** array needs promotion (`int` → `value`, an inline-payload widening
that is cheap); the **keys** stay *direct* refined strings — exactly the layout
that copies in 0.45 s today.

Mechanically this is the generic container promotion that was prototyped for the
`safe_typecast` boundary (promote components whose destination element type is
`python_value`, copy the rest as-is), restricted to the case where the
non-promoted (key) components already type-match. Non-string-keyed dicts fall
through to by-value (today's behaviour) — a documented residual, not a
regression.

- Pros: sound for the dominant dict-mutation case; **no representation change**;
  reuses the existing shared boundary and the (reverted but understood) generic
  promotion; expected to stay fast because keys remain inline strings.
- Cons: does not cover non-string-keyed dict-by-reference (still by value);
  needs `is_python_dict_type` to see through `struct_tag` at the wrap site (dict
  parameter types surface as `ID_struct_tag`, unlike list's inline `ID_struct`)
  — a small, contained fix.
- Validation: must confirm `nondet_dict2` and string-keyed dict-mutation repros
  stay sub-second and the sweep holds PASS 2905.

### Option C — status quo (accept the residual)

Leave dict parameters by value; document that mutation through a dict parameter
is not modelled. Lowest effort, but the latent unsoundness (a function that
mutates a dict argument can be falsely proved correct) remains.

---

## Recommendation & phasing

1. **Now / near-term: Option B.** Deliver sound by-reference for string-keyed
   dict parameters via values-only promotion, keeping keys inline. This is the
   high-value, low-risk slice and removes the unsoundness for the common case.
2. **Separately scoped: Option A spike.** Prototype inlining the refined string
   into `python_value` *as a measurement first* (re-run the diagnosis table). If
   it restores performance, it unblocks the uniform `dict[value, value]` default
   and therefore by-reference for non-string-keyed dicts; if it does not, the
   global default is off the table and Option B is the end state.
3. Only after A is proven on performance should the bare-`dict` default be
   flipped and the generic promotion re-enabled for dict parameters generally.

---

## What has already landed (foundation, keep regardless of the above)

These are sound correctness fixes for heterogeneous (`dict[value, value]`)
dicts — which already arise from heterogeneous literals — and are prerequisites
for any value-keyed path:

- `value_equal(a, b)` — tag-aware equality of two `python_value` operands
  (string content via the string solver for STR, scalar payload otherwise).
- Value-keyed dict **subscript reads** route through `value_equal` so a string
  key matches by content (a struct compare would compare data pointers) and a
  non-string key cannot spuriously match an int query via its unused
  `__int_val` slot. Missing keys still raise `KeyError`.
- Value-keyed dict **membership** (`in`) routes through `value_equal`; the
  constant-key fast path is skipped for value keys (it cannot read wrapped
  keys).
- The query is wrapped once per access rather than once per key.

All landed with all three python suites green and the ESBMC sweep at PASS 2905
(only the known `github_3836` ↔ `github_3836_fail` recursion artifact differs).

The generic component-iterating container promotion (list/dict/set in one place)
was prototyped at the `safe_typecast` boundary and reverted as premature; it is
the natural implementation vehicle for Option B (values-only promotion) and for
Option A's dict path once the representation allows it.

---

## Open questions

- **Does inlining actually fix the string-refinement explosion?** The refined
  string retains a `data` pointer even when the wrapper is inline. Option A is
  contingent on a spike confirming this.
- **`struct_tag` vs `struct` for dict types.** `is_python_dict_type` matches
  `ID_struct` only, but dict parameter types surface as `ID_struct_tag`; the
  predicate (or its call sites) must resolve the tag for any dict-by-reference
  wrap to fire. Small, but required for Option B.
- **Key-array width.** `PYTHON_MAX_DICT_SIZE = 16`; the by-value copy and any
  promotion are linear in this. Not the dominant cost today, but relevant if the
  bound is raised.
