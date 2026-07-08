# `python_value` TUPLE tag — design & phased plan

**Status:** spike complete (2026-07-08), implementation deferred (see
*Proportionality* below). This is the whole-group fix for the residual
list-of-tuples soundness class documented in the architecture doc's mutation-
oracle campaign entry.

## Problem

`python_value` (the tagged-union boxed value; `src/python/python_value_type.h`)
has tags NONE/INT/FLOAT/BOOL/STR/LIST/CLASS/DICT/COMPLEX/SET/CLOSURE — **no
TUPLE**. So `wrap_value` (`python_converter.cpp` ~3281) boxes a `python_tuple`
struct through the generic *CLASS fallback*: tag `CLASS` + an `address_of` into
`__class_ptr`.

Consequences (all confirmed by the mutation-oracle under `PLR_WIDE`):

1. **Comparison by pointer.** A CLASS-tagged tuple compared with a plain
   `equal_exprt` compares the union bits incl. `__class_ptr` — two equal tuples
   built at different sites have distinct pointers → compare *unequal* → `!=` is
   wrongly PROVED (a false proof). `python_value_structural_eq`
   (`python_converter_compare.cpp` ~24) has **no TUPLE case**; its CLASS case is
   a sound nondet, but the false proofs arise on the paths that use plain
   `equal_exprt` for `python_value` elements rather than `structural_eq`.
2. **Heterogeneous containers.** A `list[python_value]` mixing tuples and
   scalars (e.g. `list(zip(..)) + [scalar]`, `list(enumerate(..))` through
   set/sorted/slice) mis-handles the tuple elements. The concat mis-typing was
   fixed separately (`388d8c9b01`, sound nondet); the residual is the
   comparison/identity of tuple-tagged values inside such lists.

## Why a TUPLE tag (whole-group, not point fix)

Every list-of-tuples-through-operation bug shares one root: a tuple cannot be
*soundly and precisely* boxed into `python_value`. Fixing each operation
(set/sorted/slice/concat/compare) individually is a series of point fixes; a
TUPLE tag fixes the representation once and every operation inherits correct
behaviour (or a sound nondet) through the shared box/unbox/compare helpers.

## Representation

Reuse the existing `__class_ptr` (or `__list_ptr`) opaque pointer slot — a tuple
is boxed exactly like DICT/SET: tag `TUPLE`, `__class_ptr = address_of(tmp)`
where `tmp` is a materialised copy of the `python_tuple` struct. No new struct
field is needed (the tag discriminates the pointee), so `python_value` stays
fixed-width (byte_extract-valid — see the string-boxing rationale in
`python_value_type.h`).

## Blast radius (spike measurement)

Comparable to the SET tag (added recently). Sites needing a TUPLE case:

- `python_value_type.h`: `enum python_type_tagt { … TUPLE = 11 }`.
- `wrap_value` (`python_converter.cpp` ~3281): box `is_python_tuple_type` structs
  with TUPLE tag *before* the CLASS fallback (mirror the DICT/SET branches).
- `unwrap_value` (`python_converter.cpp` ~2390): TUPLE → dereference
  `__class_ptr` as the concrete tuple struct.
- `python_value_structural_eq` (`python_converter_compare.cpp` ~24): TUPLE case —
  dereference both, compare component-wise recursively (reuse the per-component
  numeric/string/nested logic); sound nondet when arities/component types differ.
- truthiness (`python_converter.cpp` ~2509/2929): TUPLE → `len != 0` (empty
  tuple is falsy).
- `isinstance` (`python_converter_call_builtins.cpp` ~4429/4685): TUPLE →
  `isinstance(x, tuple)`.
- `len()` and the Any-receiver method dispatch: TUPLE → tuple length / methods.
- match-statement class-pattern dispatch (`python_converter_statement.cpp` ~811)
  and expression truthiness (`python_converter_expressions.cpp` ~1946): audit
  the CLASS branches so a now-TUPLE-tagged value is still handled.

A `python_value_tuple(pv)` unwrap helper (sibling of `python_value_list`) keeps
the deref-as-tuple cast in one place.

## Phasing (each phase validation-gated: suite green, sweep 2719/0-reg, oracle
0-NEW, both fuzzer gates incl. `PLR_WIDE` negated)

- **P1 — box + structural equality.** Add the tag; box tuples as TUPLE; add the
  `structural_eq` TUPLE case (precise component-wise, nondet fallback); ensure
  the default `python_value` element comparison routes tuple-tagged values
  through `structural_eq` (never plain pointer `equal_exprt`). Closes the
  comparison false-proof class. **This is the soundness-critical phase.**
- **P2 — unwrap / truthiness / len / isinstance.** Add TUPLE cases so a boxed
  tuple read back / tested / measured / type-checked behaves correctly (P1
  leaves these as whatever the CLASS fallback did — audit for soundness first;
  make nondet where not precisely modelled).
- **P3 — precision.** Precise tuple element access, iteration, and
  materialisation of tuple-tagged values through set/sorted/slice.

## Soundness reasoning

TUPLE-tagged comparison must be *sound* in P1: precise component-wise equality
when both pointees are statically-shaped tuples of matching arity; **nondet
otherwise** (never pointer inequality). This converts the current false proofs
into (sound) false alarms at worst. The `is_python_tuple_type` guard in
`rebuild_list_as_pv` / list-concat (already landed, `388d8c9b01`) stays as the
belt-and-suspenders sound floor until P1/P3 make tuple boxing precise.

## Proportionality (why deferred)

The residual this closes is **4 `PLR_WIDE`-only negated false proofs**, none of
which reproduce minimally (all minimal set/sorted/slice-of-list-of-tuples forms
are already sound after the 2026-07-08 batch). The standing (narrow) fuzzer gate
and the oracle are at **0**. Given the change touches ~10 tag-dispatch sites with
real regression risk (retagging tuples CLASS→TUPLE alters truthiness / isinstance
/ match dispatch for any tuple-in-value), implementing it now is disproportionate
to the gated, documented residual. Recommended trigger to implement: either the
residual class widens (more real-world / oracle-corpus hits), or a broader
tuple-in-container feature (e.g. `dict.items()`, `zip` pipelines) is prioritised.
