# Design: faithful numeric byte encoding (DISPLAY / COMP-3 / COMP)

Status: **partly implemented** (stages 1–2; see note). Author: Kiro.

> **Implementation status.** Stage 1 (carry `usage`/`sign` on
> `item_infot`) and stage 2 (faithful **unsigned zoned `DISPLAY`**
> encoding for fields a different-category item aliases, via the
> pay-as-you-go classifier of §4.1) are implemented in
> `cobol_typecheck.cpp` (`usage_of`, `zoned_bytes`, `decode_zoned`,
> `encode_zoned`, `classify_record_aliases`, and the deferred numeric
> VALUE encoding in `finalize_record`). A within-record REDEFINES of an
> unsigned DISPLAY numeric as alphanumeric now reads the digit string.
> The `IS NUMERIC` / `IS ALPHABETIC[-LOWER|-UPPER]` class conditions are
> now exact over an alphanumeric item's bytes (§3.3) — a precision win
> independent of the numeric codecs, since an alphanumeric field's bytes
> are always its content. Packed-decimal (**COMP-3**, signed and
> unsigned) is now faithful too, and read/write/VALUE dispatch on USAGE
> through one codec point (`has_faithful_codec` gates which encodings the
> classifier may mark). Still to do: big-endian **COMP** (the binary
> value model is correct for arithmetic; only byte observation of a COMP
> field differs, and faithful storage would need a byte-swap on the write
> path — low value, deferred); signed zoned overpunch and `SIGN …
> SEPARATE` sizing (charset-dependent / changes layout); exact class
> conditions on faithful *numeric* fields; and edited `MOVE` (§6 step 6).
> (**01-level REDEFINES** aliasing and unsigned sign-on-store are now
> implemented — soundness items S3 and S1 in the architecture doc.) The
> migration-plan steps below track the remaining items.

This document designs replacing the frontend's uniform binary numeric
storage with **USAGE-faithful byte encodings** — zoned decimal
(`DISPLAY`), packed decimal (`COMP-3`/`PACKED-DECIMAL`), and binary
(`COMP`/`BINARY`/`COMP-5`) — so that byte-level observations of numeric
fields match IBM Enterprise COBOL.

Grounded in the IBM Enterprise COBOL for z/OS 6.4 Language Reference
(*LR*): "USAGE clause", "PICTURE clause", "SIGN clause", and the data
representation appendix; plus *z/Architecture Principles of Operation*
for decimal/binary formats.

---

## 1. Problem statement

### 1.1 What the frontend does today

Storage is byte-addressed and correct in **size**: every `01`/`77`/FD
record is a `array[N] of unsignedbv(8)` symbol, and a field is a
`byte_extract`/`byte_update` view at its offset. `phys_size_of`
(`cobol_typecheck.cpp`) computes `byte_size` per `USAGE` exactly as the
LR prescribes (DISPLAY = 1 byte/digit, COMP = 2/4/8, COMP-3 =
`ceil((digits+1)/2)`, COMP-1/2 = 4/8).

But the **content** is *not* USAGE-faithful. `item_infot` does not even
store the usage; `phys_type(item)` is just `signedbv(byte_size*8)`, and:

- `read_field` = `byte_extract(record, offset, signedbv(byte_size*8))`
  cast to the 64-bit value domain.
- `encode_numeric` = rescale → `mod 10^digits` → cast to
  `signedbv(byte_size*8)`, stored by `byte_update`.

So every numeric, whatever its USAGE, is stored as a **little-endian
two's-complement binary integer** spanning its byte width. USAGE affects
only the width.

### 1.2 The deviation, measured

`01 WS-NUM PIC 9(3) VALUE 123.` `01 WS-RED REDEFINES WS-NUM PIC X(3).`
Real IBM COBOL stores `WS-NUM` as zoned decimal — the characters
`'1' '2' '3'` — so `WS-RED = '123'`. The current model stores the binary
`123` = bytes `7B 00 00`, so `assert WS-RED = '123'` **FAILS**
(experiment confirmed). Every numeric byte observation is similarly off:

- **`REDEFINES` across a numeric/alphanumeric boundary** (the corpus has
  740 `REDEFINES`): reinterpreting numeric bytes as characters (or vice
  versa) sees binary, not zoned/packed bytes.
- **Class conditions** `IS [NOT] NUMERIC` / `ALPHABETIC` (corpus: ~64):
  these test the *character/zone content* of a field, which the value
  model has no representation for, so they are modelled
  nondeterministically today.
- **Group/byte `MOVE` spanning a numeric** copies binary bytes; faithful
  to itself but not to what a real dump or a cross-category alias holds.
- **`MOVE` of an edited or alphanumeric source to a numeric receiver**
  (de-editing) and **`MOVE` to an edited PICTURE** (insertion of
  `Z . , + - $ CR DB /`) depend on character content; today the
  de-edited value is nondet and editing is not applied (corpus: ~163
  edited PICs).
- **Sign handling**: zoned overpunch / packed sign-nibble / `SIGN
  SEPARATE` are not represented.
- **Endianness**: `COMP`/`BINARY` on z/Architecture is **big-endian**;
  the model is little-endian.

The value-domain model is *exactly right* for arithmetic (decimal scale
is exact — the whole point), and for any program that only computes with
and compares numerics by value. It is wrong only at **byte-observability
points**: aliasing, class tests, cross-category moves, editing.

---

## 2. Design goals

1. Numeric fields hold **USAGE-faithful bytes**: zoned `DISPLAY`, packed
   `COMP-3`, binary `COMP`/`COMP-5`, with correct sign representation.
2. `REDEFINES` aliasing, `IS NUMERIC`/`ALPHABETIC` class tests, and
   cross-category `MOVE`s become **exact** rather than nondet.
3. Keep decimal arithmetic **exact** (do not regress the value model's
   one real strength).
4. Bound the symbolic-execution cost (nibble/byte arithmetic is more
   expensive than a single integer).
5. Charset is configurable; default ASCII host (as currently pinned),
   EBCDIC behind a flag.

---

## 3. The core idea — USAGE-aware codecs over the byte array

Make the byte array the **single source of truth**, and turn
`read_field`/`encode_numeric` into **USAGE-specific codecs** between the
value domain (`signedbv(64)` at a decimal scale) and the field's bytes.
Then `REDEFINES` "just works": an alias reads the same bytes through its
*own* codec; a class condition reads the bytes directly; arithmetic still
works on the decoded value.

### 3.1 Carry the encoding on the item

Add to `item_infot` an explicit encoding descriptor (the information
`phys_size_of` already consumes but throws away):

```cpp
enum class usaget { DISPLAY, PACKED, BINARY, NATIVE_BINARY, FLOAT_SHORT, FLOAT_LONG };
enum class signt   { UNSIGNED, OVERPUNCH_TRAIL, OVERPUNCH_LEAD,
                      SEPARATE_TRAIL, SEPARATE_LEAD };
usaget usage;
signt  sign;
```

`phys_type` and the read/write helpers branch on `usage`.

### 3.2 Codecs (LR data representation)

For value `v` (signed integer at the item's scale; the decimal point is
implied and not stored — LR "PICTURE clause", `V`):

- **DISPLAY / zoned decimal** (LR "USAGE … DISPLAY", external decimal).
  One byte per digit. Digit byte = `zone | digit` where `zone` is `0x3`
  (ASCII) / `0xF` (EBCDIC). Sign (signed `S9`) is an **overpunch** on the
  zone nibble of the sign digit (trailing by default; LR "SIGN clause"):
  positive/unsigned zone, negative encoded by the EBCDIC `0xD` / ASCII
  convention. `SIGN SEPARATE` puts a `'+'`/`'-'` byte before/after.
  Encode: emit digits high→low via `(v / 10^k) % 10`; decode: sum
  `digit(byte_i) * 10^k`, apply sign.

- **PACKED-DECIMAL / COMP-3** (LR "USAGE … PACKED-DECIMAL").
  Two digits per byte, most-significant first; the **low nibble of the
  last byte is the sign**: `0xC` positive, `0xD` negative, `0xF`
  unsigned. `digits` is odd-padded to fill the high nibble of byte 0.
  Encode/decode = nibble pack/unpack with the sign nibble.

- **BINARY / COMP / COMP-4** (LR "USAGE … BINARY").
  Two's-complement, **big-endian** (z/Architecture), width per
  `phys_size_of`. The picture still bounds the value (`PIC S9(4) COMP`
  truncates mod 10^4 on store — LR "BINARY … truncation"). Decode =
  big-endian `byte_extract` + sign-extend; encode = `mod 10^digits`
  (unless `TRUNC(BIN)`/`COMP-5`) then big-endian bytes.

- **COMP-5 / NATIVE_BINARY**: like BINARY but **no decimal truncation**
  (full bit-width range) and native endianness (LR "COMP-5").

- **COMP-1 / COMP-2** (short/long float): a **separate value kind**
  (IEEE `floatbv`), not an integer-at-scale. Deferred — the value domain
  has no float; absent from the corpus (0 occurrences). Documented as the
  remaining gap.

All codecs are pure `exprt` constructions (nibble masks via
`bitand`/`shl`/`concatenation` over `byte_extract`s), so symbolic
execution reasons about them precisely and `REDEFINES` aliases of
different categories interoperate exactly.

### 3.3 What becomes exact

- `REDEFINES` numeric↔alphanumeric: the alias reads the real zoned/packed
  bytes — the `WS-RED = '123'` experiment would now hold.
- `IS NUMERIC` / `IS ALPHABETIC`: decode the bytes and test that each
  byte is a valid digit/zone (numeric) or letter/space (alphabetic) — LR
  "Class condition". No longer nondet.
- De-editing `MOVE` alphanumeric→numeric and editing `MOVE`→edited PIC:
  now expressible as character transforms over real bytes.
- Group/byte `MOVE` and dumps observe IBM-faithful content.

---

## 4. Cost and the dual-representation optimization

Nibble/zone arithmetic on **every** numeric read/write is markedly more
expensive for the SAT/SMT back-end than one integer `byte_extract`. Two
ways to contain it:

### 4.1 Lazy / observation-driven encoding (recommended first)

Most numerics are only ever read and written **by value**. Keep the fast
value-domain path as the default, and materialize the faithful byte
encoding **only for fields whose bytes are actually observed** at a
different category — detected statically at layout/typecheck time:

- a field that is (transitively) `REDEFINES`-aliased by an item of a
  different category, or that aliases one;
- a field that is the subject of a class condition;
- a field crossed by a group/byte `MOVE` to/from a different category;
- a field moved to/from an edited PICTURE.

Fields not so observed keep today's binary value model (no cost, no
regression). This makes the change **pay-as-you-go** and bounds the
blast radius — the same principle the plan already uses ("carry the
extra representation only when the operation needs it").

The correctness obligation is **consistency**: a field flagged for
faithful encoding must use the codec on *every* access (read and write),
never mixing the binary and zoned/packed views of the same bytes. The
static flag guarantees that.

### 4.2 Single-source-of-truth codecs (cleaner, heavier)

Always store faithful bytes and always go through the codec. Simpler to
reason about (no per-field mode), but pays the codec cost everywhere.
Recommended only if §4.1's static classification proves too conservative
or buggy in practice.

---

## 5. Charset and endianness

- **Charset**: zoned digits and class tests depend on ASCII vs EBCDIC
  zone nibbles and collating order. The plan pins ASCII host; add a
  `--charset ebcdic` switch that selects zone `0xF`, EBCDIC sign
  overpunch, and the EBCDIC collating sequence for alphanumeric compares.
  Default stays ASCII so existing tests are unaffected.
- **Endianness**: switching `COMP` to big-endian is IBM-faithful but
  changes the bytes seen by existing `COMP` `REDEFINES` tests. Gate
  behind the same dialect/target notion; default can stay little-endian
  (host) until a test needs z/Arch byte order, with the deviation
  documented.

---

## 6. Migration plan (incremental, suite stays green)

1. **Carry `usage`/`sign` on `item_infot`** (populate from the
   `phys_size_of` call site). No behaviour change. Build green.
2. **Implement + unit-test the codecs** in isolation (encode∘decode =
   identity within picture range; known vectors: `123` zoned ASCII =
   `31 32 33`; `-123` COMP-3 = `12 3D`). Not yet wired into read/write.
3. **Static observation classifier** (§4.1): mark fields needing faithful
   encoding. Initially mark *none* (pure no-op) to prove the plumbing.
4. **Enable faithful encoding for `REDEFINES`-aliased fields**; add the
   `WS-RED = '123'` regression (now SUCCESS). Re-baseline CardDemo
   (must stay 33/44 default; watch the 740 `REDEFINES`).
5. **Make `IS NUMERIC`/`ALPHABETIC` exact** for faithfully-encoded
   fields; keep nondet otherwise. Add positive/negative class tests.
6. **Edited / de-editing `MOVE`** over faithful bytes (largest piece;
   may stay value-nondet initially).

Risks and mitigations:

- *Performance regression* on programs heavy in flagged numerics —
  measure CardDemo solve time at step 4; keep §4.1 conservative.
- *Consistency bugs* from mixing views — enforced by the per-field flag
  and codec-only access; covered by encode∘decode unit tests.
- *Charset/endianness churn* in existing tests — gated behind flags,
  defaults unchanged.

---

## 7. Relationship to the value model and the PERFORM redesign

This change is **orthogonal** to the PERFORM/control-flow redesign
(`cobol-perform-control-flow-design.md`): one reworks data
representation, the other control flow. Both can proceed independently.
The value-domain arithmetic core is *retained* — faithful encoding is a
codec layer at the byte boundary, not a replacement for the exact
decimal arithmetic that is the frontend's main correctness asset.
