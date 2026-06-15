# Design: character-content semantics

Status: design (not yet implemented). Author: Kiro.

This document designs precise modelling of the COBOL constructs that
manipulate the **character content** of items — `STRING`, `UNSTRING`,
`INSPECT`, the string intrinsic functions on item arguments, reference
modification with a non-constant length, and the numeric↔alphanumeric
comparison/de-editing. These are imprecisions **I2, I3, I8, I10, I14** in
`cobol-frontend-architecture.md` §5 (and the Tier-2 item in the
suggested roadmap). It is referenced from that architecture document and
from `cobol-precision-and-gaps-plan.md` §1.

Grounded in the IBM Enterprise COBOL for z/OS 6.4 Language Reference
(*LR*): "STRING statement", "UNSTRING statement", "INSPECT statement",
"Intrinsic functions", "Reference modification", "Comparison of numeric
and nonnumeric operands".

---

## 1. Problem and the key insight

Today these verbs **havoc the items they write** (a sound
over-approximation; see the architecture doc's "character-level verbs
over a value model" decision). So `STRING 'AB' 'CD' INTO X` leaves `X`
nondeterministic, `INSPECT … TALLYING c` leaves `c` nondeterministic,
etc. That blocks proving any property that depends on the result.

**Key insight: no new data representation is needed.** Storage is already
byte-addressed — every record is an `array[N] of unsignedbv(8)` and a
field is a `byte_extract`/`byte_update` view at a known offset. The
character content *is* those bytes. The reason the verbs are imprecise is
simply that they do not yet *compute over* the bytes. We already have a
working precedent: the exact `IS NUMERIC`/`ALPHABETIC` class condition
(shipped) reads each byte of an alphanumeric item with `byte_extract` and
ANDs a per-byte range test. The character-content verbs are the same idea
generalised.

So the "character-interpretation layer" is not a type; it is:
1. a small set of **byte helpers** over an item's `(record, offset,
   length)`, and
2. **bounded unrolling** over the item's *compile-time* size, with a
   **runtime pointer/counter** where COBOL has one.

Item sizes are known at translation time (from the PICTURE), so every
loop here is statically bounded and can be unrolled into straight-line
`exprt`/`codet` — no CBMC loop, no `--unwind` needed.

---

## 2. Building blocks

- `byte_at(record, offset, i)` → `byte_extract(record, offset+i, u8)`:
  the i-th character of an item (endianness-independent, single byte).
- `set_byte(record, offset, i, value)` → a `byte_update`. A whole-item
  write is a sequence (or one array `byte_update`).
- **Bounded scan** for a delimiter / character: unroll `i = 0 … size-1`,
  building an `if_exprt` chain that yields the index of the first match
  (or "not found"); used by `UNSTRING … DELIMITED BY` and `INSPECT`.
- **Runtime pointer** `p` (an integer symbol): the current 1-based
  position in a receiver/sender. `STRING … WITH POINTER p`, `UNSTRING …
  WITH POINTER p`, and the implicit pointer all use it; writes are
  `if(p-1+k < size) set_byte(…); p := p + …`. Unrolling over the fixed
  size lets the conditional writes use a runtime `p`.
- **Numeric operands**: a numeric item used where character content is
  observed (a `STRING` sender, a numeric↔alnum comparison) must present
  its display (zoned) bytes. This reuses the faithful-encoding classifier
  (`cobol-numeric-encoding-design.md`): extend
  `classify_record_aliases` so a numeric "observed by a character verb /
  cross-category comparison" is marked `faithful_bytes`. Then its bytes
  *are* its digits and the byte helpers apply uniformly.

---

## 3. Per-construct lowering

### 3.1 INSPECT … TALLYING (do first — simplest, countable)
LR "INSPECT statement", TALLYING phrase. For `INSPECT item TALLYING c FOR
ALL lit`: unroll over the item's bytes; `c := c + Σ_i (window at i equals
lit)`. `LEADING`/`CHARACTERS`/`BEFORE`/`AFTER` add guards (a "counting
enabled" flag that flips at the BEFORE/AFTER delimiter, tracked across the
unrolled positions). `INSPECT … REPLACING` writes bytes conditionally
(unrolled). Exact and bounded.

### 3.2 STRING (concatenation)
LR "STRING statement". Parse each sending operand and its `DELIMITED BY
{SIZE | lit | id}`. Maintain pointer `p` (from `WITH POINTER` or 1).
For each sender, for each source byte position `k` (unrolled over the
sender size): if not yet at the delimiter and `p-1 < receiver-size`, write
the byte at `p` and increment `p`. `ON OVERFLOW` becomes the *exact*
guard `p-1 > receiver-size` (replacing the current nondet guard in
`parse_overflow_phrase` for STRING/UNSTRING). Receiver bytes past the
written prefix are left unchanged (LR: STRING does not space-fill).

### 3.3 UNSTRING (split)
LR "UNSTRING statement". Scan the source (unrolled) for each `DELIMITED
BY` delimiter, copy the field into the next receiver, set its `COUNT IN`
and `DELIMITER IN`, advance, and bump the `TALLYING` count. Bounded by
the source size and the number of receivers (both compile-time).

### 3.4 String intrinsics on items (I14)
LR "Intrinsic functions". `UPPER-CASE`/`LOWER-CASE`/`REVERSE`/`TRIM` over
an item: produce the result bytes from the source bytes (per-byte map for
case, index reversal for REVERSE, leading/trailing-space scan for TRIM).
Already done for *literals*; this extends them to item arguments using
the byte helpers.

### 3.5 Reference modification with a non-constant length (I8)
LR "Reference modification". `id(start:len)` with a runtime `len`: the
result is a `len`-byte view starting at `start`. With the byte helpers a
read/compare can be unrolled to the item's max size with `i < len`
guards, and a bounds `ASSERT (start>=1 ∧ start+len-1<=size)` emitted.

### 3.6 Numeric ↔ alphanumeric comparison and de-editing (I2, I3)
LR "Comparison of numeric and nonnumeric operands" / "MOVE statement".
Once the numeric operand is faithfully zoned (§2), the comparison is a
byte compare and `MOVE alnum → numeric` is a de-edit (scan digits) over
the bytes. Depends on the classifier extension.

---

## 4. Cost and mitigation

Unrolling is O(item size) `exprt` per operation. Typical COBOL string
fields are tens of bytes, so this is cheap. For an unusually large item
(say > a few hundred bytes) the unrolled term could be large; mitigation:
cap the unroll at a configurable bound and fall back to the current
havoc beyond it (sound), or keep havoc for items above the cap. The
`IS NUMERIC` precedent already unrolls over item size with no measured
problem on CardDemo.

This is the same **pay-as-you-go** discipline used for numeric encoding:
spend the precise (more expensive) modelling only where the construct is
actually used, and keep the cheap default elsewhere.

---

## 5. Migration plan (each step: build, keep suite green, re-baseline
CardDemo 44/44 at --unwind 3, promote the relevant KNOWNBUG test)

1. **Byte helpers** + **INSPECT TALLYING FOR ALL/LEADING** exact
   (promote `knownbug-inspect-precise`). Smallest, countable, no pointer.
2. **STRING** with exact concatenation, pointer, and exact ON OVERFLOW
   (promote `knownbug-string-precise`).
3. **UNSTRING** split with COUNT/DELIMITER/TALLYING.
4. **String intrinsics on items** (promote `knownbug-intrinsic-item`).
5. **Classifier extension** for numeric operands of character verbs /
   cross-category comparison, enabling **I2/I3** (promote
   `knownbug-num-alnum-cmp`, `knownbug-move-alnum-num`).
6. **Reference modification non-constant length** (I8).

Risks: term-size blow-up on large items (mitigation §4); INSPECT's
BEFORE/AFTER/LEADING state machine is fiddly (test thoroughly); the
classifier extension must not change the CardDemo baseline (pay-as-you-go
keeps the blast radius to observed fields).

---

## 6. Relationship to other documents

- Reuses the byte-addressed storage and the faithful-encoding classifier
  of `cobol-numeric-encoding-design.md` (numeric operands → zoned bytes).
- Independent of `cobol-perform-control-flow-design.md`.
- Closes architecture-doc items I2, I3, I8, I10, I14 as it lands; the
  architecture doc and `cobol-precision-and-gaps-plan.md` §1 link here.
