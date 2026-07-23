# Python frontend: sound exact-`Decimal` model

Status: **P1/P2/P3 landed** (`8f8ebf3aea`, `2fcd9c7b46`; `!=` class
dispatch `7367a713bf`). Remaining residuals: non-terminating division and
`sqrt` (28-digit context rounding exceeds the 64-bit coefficient → sound
nondet), Inf-comparison precision (sound nondet), and the bounded
exponent-alignment / 64-bit coefficient. See
[§9 of the plans doc](python-frontend-plan.md#precision).

This deep-dive specifies a sound, exact model of the `decimal.Decimal`
stdlib type for the CBMC Python frontend, replacing the previous
float-wrapper stub (which was unsound for exact decimal arithmetic).

## 1. Why not `float` / `fixedbv`?

The previous stub modelled `Decimal` as a `float` (`self._v`). That is
**unsound** for exact decimal semantics: `Decimal("0.1") + Decimal("0.2")
== Decimal("0.3")` is `True` for `Decimal` but `False` in IEEE float (and
the stub could therefore "prove" false things, or fail true ones).

`fixedbv` does **not** help: it is *binary* fixed-point (an integer scaled
by a power of **2**), so it cannot represent 0.1/0.2/0.3 exactly either —
the same base-2 limitation as `float`. The exactness problem is
**base-10 vs base-2**, orthogonal to fixed-vs-floating.

The only sound exact representation is **base-10**: a value is

```
(-1)^sign  ×  coefficient  ×  10^exponent
```

with an integer coefficient — exactly CPython's internal
`(_sign, _int, _exp)` tuple. This also directly satisfies the
representation-sensitive accessor tests (`decimal`, `decimal4`).

## 2. Representation

`Decimal` is the stub class with these instance fields (so the existing
method-dispatch machinery applies unchanged):

| field | type | meaning |
|---|---|---|
| `_sign` | int (0/1) | 0 = positive, 1 = negative (CPython convention) |
| `_int` | int | unsigned coefficient (`"10.5"` → 105) |
| `_exp` | int | base-10 exponent (`"10.5"` → −1) |
| `_is_special` | int (0/1) | 1 for Inf/NaN |
| `_special_kind` | int | 0=finite, 1=+Inf, 2=−Inf, 3=NaN, 4=sNaN |

Value of a finite Decimal = `(-1)^_sign × _int × 10^_exp`. Equality and
ordering are **value**-based (so `1.0` ≡ `1.00`), while the representation
(`_int`, `_exp`) is preserved for the accessors and for arithmetic
results (`1.5 + 2.5` → `_int=40, _exp=-1`).

## 3. Architecture — hybrid (stub + thin converter intrinsic)

Comparison and arithmetic over `(_sign, _int, _exp)` using `10 ** Δexp`
alignment **verify in the frontend** (confirmed by probe), so the bulk of
the model lives in readable Python in `src/python/library/decimal.py`.

The one thing the frontend cannot do is parse a **string literal** at
construction (`float("…")` / parsing a runtime string is nondet). That
single piece is a converter intrinsic: when the callee is `Decimal` and
the argument is a **string/int literal**, the converter parses it to
`(sign, coeff, exp)` and builds the typed `Decimal` struct directly
(`build_decimal_literal`), bypassing `__init__`. Non-literal construction
(`Decimal(x)`, `Decimal(user_string)`) falls through to the stub
`__init__` (int / Decimal-copy handled; symbolic string → conservative).

Generalisation note: this is one instance of a broader pattern —
*literal-constructed stdlib value types* (here `Decimal`; potentially
`Fraction`, etc.) where the converter folds a constant constructor
argument into a typed struct. Kept `Decimal`-specific for now; revisit if
a second case appears.

## 4. Construction (literal parsing, converter)

Grammar subset: `[-+]? digits [.digits]? ([eE][-+]?digits)?`, plus the
special words `inf/infinity/nan/snan` (case-insensitive).

- strip sign → `_sign`; drop the `.`; `_int = int(digit-string)`;
  `_exp = -(#fractional digits) + explicit-E-exponent`.
- `"10.5"` → `(0, 105, -1)`; `"1.00"` → `(0, 100, -2)`; `"3"` → `(0, 3, 0)`;
  `"-0"` → `(1, 0, 0)`; `"1.5e3"` → `(0, 15, 2)`.
- int literal `Decimal(42)` → `(value<0, |value|, 0)`.
- `> 18` coefficient digits, or a malformed string → fall through
  (conservative), never a wrong exact value.
- float literal `Decimal(1.1)` → residual (do **not** treat as exact).

## 5. Operations (stub, integer arithmetic only)

Alignment helper: bring both operands to a common exponent
`e = min(e1, e2)` and form signed coefficients
`sa = (-1)^s1 · c1 · 10^(e1-e)`, `sb = (-1)^s2 · c2 · 10^(e2-e)`.

- `__eq__`: NaN-aware (NaN ≠ everything incl. itself); else `sa == sb`
  (zero is sign-insensitive).
- `__lt__ / __le__ / __gt__ / __ge__`: NaN → False; else compare `sa,sb`.
- `__add__ / __sub__`: `r = sa ± sb`; result `(sign(r), |r|, e)`.
- `__neg__ / __abs__`: flip / clear `_sign`.
- `__mul__`: `(s1^s2, c1·c2, e1+e2)`.
- `__floordiv__ / __mod__`: integer divide/remainder of the aligned
  values, truncating toward zero (matches Decimal for the integer cases
  exercised; general non-integer `//`/`%` is a residual).
- `__truediv__ / sqrt / quantize`: **sound residual** — return a
  conservative nondet finite Decimal (never the removed float value), to
  be replaced by round-to-context in P3.
- `is_zero / is_signed / is_finite / is_infinite / is_nan`: from fields.

## 6. Soundness & bounds

- **Coefficient width.** Frontend Python `int` is 64-bit
  (`signedbv[64]`) *everywhere*, so a 64-bit coefficient is consistent
  with the frontend-wide int model — Decimal introduces no *new*
  unsoundness. Values/aligned products beyond 64-bit wrap (the same
  pre-existing approximation as all frontend int arithmetic). A future
  hardening can add overflow model-bound asserts or widen the struct.
- **NaN/Inf.** `_is_special`/`_special_kind` drive PLR-faithful
  semantics (NaN comparisons False, etc.) rather than treating specials
  as ordinary numbers.
- **Never a false proof.** Unsupported/over-bound inputs degrade to a
  conservative nondet Decimal or fall through to ordinary construction;
  they do not fabricate a specific wrong value.

## 7. Phasing & test mapping

| Phase | Scope | Tests |
|---|---|---|
| **P1** | fields + literal parse (str/int) + `_from_parts`/`build_decimal_literal`; `__eq__`, ordering; accessors | `decimal`, `decimal2`, `decimal3` (+ `_fail`) |
| **P2** | `+,-,neg,abs,*,//,%` (parts, renormalised) | `decimal4` (+ `_fail`) |
| **P3** | specials, `quantize`, `truediv`/`sqrt` round-to-context, float construction | general programs |

P1 and P2 are landed together because the representation change from `_v`
to `(sign, coeff, exp)` would otherwise leave the arithmetic methods
referencing a removed field. Each landing: `ulimit -v`, build, three
python suites green, ESBMC sweep vs baseline (no regressions),
`clang-format-15` / `git-clang-format` clean, commit.

## 8. Risks & alternatives

- **64-bit overflow** for high-precision Decimals — inherits the
  frontend-wide int approximation; harden later.
- **`10 ** Δexp` cost** — validated working; literal exponents fold at
  convert time (the common case).
- **Division/context rounding** — genuinely large; deferred to P3 as a
  sound residual.
- **Rejected:** normalise to a single fixed scale `10^-K` — simpler
  equality/add, but destroys the preserved `_exp`/`_int` that `decimal`
  and `decimal4` assert.

## 9. Known residual — construction fold vs the `from decimal import` shape

`bool(Decimal("0"))` must be `False` (PLR: a zero Decimal is falsy), but
the default proves `assert not Decimal("0")` FAILED. The literal-parsing
fold (`build_decimal_literal`, §4) that would give the constructed value
its `_int = 0` does **not** fire for the `from decimal import Decimal`
binding shape: the name `Decimal` binds the imported CLASS object
(`__class_tag 11`), so the call is dispatched as a generic pv-CLASS
construction and `__bool__` reads a **nondet** `_int`. A probe that added
`__bool__` to the stub regressed truthy Decimals (the nondet `_int` reads
both ways), confirming the real fix is a **Decimal-model pass**: make the
`Decimal(<literal>)` construction fold fire through the imported-name
binding (recover the class from the binding's `__class_tag 11`, then route
to `build_decimal_literal`), so the constructed value carries its exact
parts and `__bool__`/comparisons read them. Sound direction unaffected
(false PROOF residual). Tracked as the P3-adjacent construction item.