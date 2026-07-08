# Retype-on-reassign — a soundness whole-group (spike 2026-07-08)

**Status:** root-caused, fix deferred (multi-path; needs a uniform rebind). This
is the ACTUAL root of the two remaining `PLR_WIDE` "tuple-tag" residuals — they
were mis-attributed to tuple boxing.

## Symptom (false proofs)

Reassigning a variable to a value of a **different aggregate element/container
type** reinterprets the bits instead of rebinding the name:

```python
xs = [6, 2]           # xs : list[int]
xs = ["a", "b"]       # reinterpreted as list[int] -> xs[0] != "a" FALSE-PROVED
xs = [7.0, 8.0]       # reinterpreted -> xs[0] != 7.0 FALSE-PROVED
xs = [(1, 2), (3, 4)] # reinterpreted -> xs[0] == (1,2) unprovable / != FALSE-PROVED
xs = list(zip(xs, xs))# the zip result (list[tuple]) reinterpreted as list[int]
```

All verified as real false proofs at `--unwind 10` (not unwinding artifacts).
The mutation-oracle found them via `list(zip(sorted(xs), xs))` then index/compare
(`/tmp/plrneg_z` rand_1874, rand_2636).

## Root

Python rebinds a name on assignment, so `xs = <different-typed value>` should
give `xs` the NEW type. Instead the frontend keeps `xs`'s FIRST-assignment CBMC
type and forces the RHS to it:

- **Assign-cast path** (`convert_assign`, ~line 3896):
  `if(typed_rhs.type() != sym.type) typed_rhs = safe_typecast(typed_rhs, sym.type)`
  — `safe_typecast` of `list[tuple]`/`list[str]` to `list[int]` reinterprets the
  element layout.
- **RHS-coercion path** (`coerce_assign_rhs(rhs, lhs.type())`, ~lines 94/828):
  a list literal whose elements CAN be coerced to the stale element type
  (str/float → int slots) is converted straight into a `list[int]`-typed struct
  with reinterpreted bits, so it never even reaches the assign-cast as a
  different type. (This is why `xs = ["a","b"]` and `xs = [7.0,8.0]` behave
  differently from `xs = [(1,2)]`, whose tuple elements cannot coerce and so hit
  the assign-cast instead — two paths, one bug.)

**Scalars are already sound:** `x = 1; x = "z"` verifies correctly (`x != "z"`
FAILED), so the issue is specific to the aggregate (list/dict/tuple/set)
reassignment paths.

## Why a partial fix was reverted

Retyping the symbol only at the assign-cast site (~3896) fixed the isolated
`xs = [(1,2)]` case (FP → sound) but NOT `xs = ["a","b"]`/`[7.0]`/`list(zip(..))`
(they take the coercion path) — an inconsistent partial fix to a whole-group, so
it was backed out (suite stayed green, but it did not close the actual
residuals).

## Fix direction (whole-group)

Make reassignment **rebind** uniformly: when the RHS's natural type differs from
the variable's prior aggregate type, RETYPE the symbol to the RHS type and assign
the RHS as-is (it already carries correct bits) — never coerce/cast to the stale
type. This must be applied consistently at BOTH paths:
1. Do not pass the variable's stale declared type as the coercion target for a
   plain (unannotated) reassignment RHS — convert the list literal to its natural
   type, then rebind.
2. At the assign-cast site, retype the symbol for a differently-typed aggregate
   RHS instead of `safe_typecast`.

Mirrors the existing retype paths: the pointer-alias retype (`convert_assign`
~3883) and the concat element-widening retype (`2eceff5929`). Soundness: rebinding
to the RHS's exact type is always sound; the risk is only precision if the same
name is later read on a path expecting the old type (Python allows this; the
pointer path already accepts it).

Open design question: annotated variables (`xs: list[int]`) SHOULD keep their
declared type and a mismatched reassignment is an annotation error
(`--python-check-annotations`), not a rebind — so the rebind applies to
UNANNOTATED names only. Validate against `check-annotations-*` tests.

## Validation gate (when implemented)

Suite green; sweep 2719/0-reg; oracle 0-NEW; both fuzzer gates incl. `PLR_WIDE`
negated (should drop the last 2 residuals to 0). New CORE guards:
`list-reassign-retype` (str/float/tuple element retypes) + `-selfref` (`xs =
list(zip(xs, xs))`).
