# Design: pointer / based-storage model

Status: design (scoping). Author: Kiro.

Grounded in the IBM Enterprise COBOL for z/OS 6.4 Language Reference:
"LINKAGE SECTION", "CALL statement", "SET statement" (format 5 — SET
pointer/ADDRESS OF, format 6 — SET ADDRESS OF), "USAGE … POINTER",
"ADDRESS OF special register".

## Goal

Model COBOL storage that is reached **by address** rather than owned
outright, so the front-end can verify:

1. **CALL … BY REFERENCE with aliasing** — the callee's `LINKAGE` record
   *is* the caller's argument storage, including when two arguments overlap
   or the callee reaches the storage by another path. (Today's copy-in /
   copy-out is exact only for non-overlapping arguments.)
2. **`SET ADDRESS OF linkage-record TO …`** (LR "SET statement" format 6)
   and `SET pointer TO ADDRESS OF item` (format 5) — rebinding a based
   record's location at run time.
3. **`USAGE … POINTER` data items** and the **`ADDRESS OF`** special
   register.

This is the one capability shared by the CALL-aliasing gap (I16 residue)
and the `SET ADDRESS OF` gap (I18); both are blocked on it.

## Why the current model cannot express this

A record is a **statically allocated byte array** symbol
`cobol::<prog>::<record>` of type `array[N] of u8`. A `reft` is
`{info, record, offset, dyn_size}` where `record` is the byte-array
`symbol_exprt`; reads are `byte_extract(record, offset, T)` and writes
`byte_update(record, offset, …)`. Every record has its own distinct
storage, so two names can never denote the *same* bytes — exactly what
aliasing requires.

The single function that turns a record name into the lvalue placed in
`reft.record` is **`record_expr(name)`** (it returns the byte-array
`symbol_exprt`). That is the architectural choke point: the whole model
assumes "record name ⇒ a fixed symbol". Generalising *that one mapping*
from "a symbol" to "an lvalue that may be a dereference of a base pointer"
is the change; because `reft.record` is already an arbitrary `exprt` that
flows into `byte_extract`/`byte_update`, the dozens of read/write/compare/
MOVE/STRING consumers need **no change** — they already operate on an
lvalue. This is the payoff of the existing `reft` abstraction (record +
offset, both `exprt`), and it is why this is an architectural
generalisation rather than a scattered rewrite.

## The architectural change: record base is an lvalue

Classify each record as **owned** or **based**:

- **Owned** (WORKING-STORAGE, LOCAL-STORAGE, FILE records, the built-in
  EIB/DFHxxx records): a static byte-array symbol, exactly as today.
- **Based** (every `LINKAGE SECTION` 01/77 record; LR "LINKAGE SECTION":
  these describe data whose storage is supplied by the caller or by
  `SET ADDRESS OF`): the record has **no storage of its own**. Instead it
  has a **base-pointer symbol** `cobol::<prog>::<record>$base` of type
  `array[N] of u8 *` (pointer to the record's byte-array type), and
  `record_expr(name)` returns `dereference_exprt{base_ptr}` instead of the
  array symbol.

`item_infot` (and the per-record metadata) gains an `is_based` flag, set
while parsing the LINKAGE SECTION (the section keyword is already
recognised — a `current_section` enum is added so `place_field` can mark
items, and `finalize_record` allocates a `$base` pointer symbol instead of
a byte array for a based record).

Reads/writes are unchanged in form:
`byte_extract(dereference(base_ptr), offset, T)` /
`byte_update(dereference(base_ptr), offset, …)`. CBMC resolves these
through the pointer, with provenance, so two based records whose `$base`
point at the same object alias soundly.

### Sub-field arguments and offsets

A based record's `$base` points at the record's first byte; intra-record
offsets are the existing `reft.offset`. When a CALL argument is a sub-field
`R(o)` (offset `o` in owned record `R`), the callee's formal `$base` is set
to `address_of(R) + o` — i.e. `address_of_exprt{ R[o] }` widened to the
record-pointer type (`pointer_typet` to `array[N]u8`, via a typecast). The
formal's own `offset` then runs 0..N within that base, so it reads
`R[o..o+N)`. Pointer + byte-offset reads use CBMC's `byte_extract` over the
dereferenced object, which already handles reading a sub-object view.

## CALL lowering (replaces copy-in / copy-out for BY REFERENCE)

At a linked `CALL … USING a b …` (see cobol-call-linkage.md):

- **BY REFERENCE** (default): assign the callee's formal `$base` =
  `&(argument storage)` before the call; **no copy-in, no copy-out**. The
  callee operates directly on the caller's bytes, so updates are visible
  and aliasing between arguments is faithful (LR "CALL statement": BY
  REFERENCE makes the callee reference the same storage).
- **BY CONTENT / BY VALUE**: allocate a fresh temporary object, copy the
  argument into it, point the formal `$base` at the temporary (a copy, not
  shared) — preserving today's semantics.

This removes the documented non-overlap assumption of the copy-in/out
model. The two-pass signature collection and forward-reference handling are
unaffected (signatures gain "is the formal based", which it always is).

## SET ADDRESS OF / ADDRESS OF / USAGE POINTER

- `SET ADDRESS OF L TO p` (format 6): `L$base = (record-ptr)p`.
- `SET ADDRESS OF L TO ADDRESS OF X`: `L$base = &X-storage` (typecast).
- `SET p TO ADDRESS OF X` (format 5): a `USAGE POINTER` item `p` holds an
  address; store `&X-storage` into it.
- `USAGE POINTER` item: an 8-byte item whose value domain is a pointer
  (`pointer_typet` to `u8`) rather than the scaled integer; `ADDRESS OF X`
  is `address_of_exprt` of X's storage. `SET ADDRESS OF L TO p` reads it
  back. (Pointer items compared/MOVEd by pointer equality / copy.)

`ADDRESS OF X` for an owned X is `&array-symbol[offset]`; for a based X it
is `base_ptr + offset`.

## Standalone verification of a LINKAGE program

When a program with a LINKAGE SECTION is verified directly (no caller),
its based records' `$base` are unbound. The entry point
(`cobol_entry_point`) must give each unbound based record **fresh backing
storage** so reads are sound nondeterministic inputs rather than invalid
dereferences: allocate a static (or `__CPROVER_allocate`) byte array per
top-level LINKAGE record and set `$base` to its address in
`__CPROVER_initialize`. This preserves the current behaviour that LINKAGE
inputs read as nondet, now via a real object the pointer targets.

## Soundness

Aliasing becomes *exact* (shared object ⇒ shared bytes; CBMC tracks pointer
provenance), strictly improving on copy-in/out. The only new failure modes
are genuine: an invalid `SET ADDRESS OF` / dangling pointer surfaces as a
pointer-safety violation under `--pointer-check`, which is correct (it is a
real fault). Owned records are byte arrays exactly as today, so the
existing suite and the value/faithful-encoding models are untouched.

## Phasing (each an independently testable increment)

1. **Record classification + `record_expr` via base pointer.** Add
   `is_based`/`current_section`; allocate `$base` for LINKAGE records;
   `record_expr` returns a dereference for based records; entry point
   allocates backing storage and binds `$base`. At this point a standalone
   LINKAGE program behaves as before (nondet inputs), validating the
   indirection end-to-end with zero behavioural change — the safest first
   step.
2. **CALL BY REFERENCE via `$base` binding** (drop copy-in/out); keep
   copy semantics for BY CONTENT/VALUE. Add an aliasing test (two
   arguments sharing storage) that copy-in/out gets wrong and this gets
   right.
3. **`SET ADDRESS OF` (formats 5/6) + `ADDRESS OF`.**
4. **`USAGE POINTER` data items** (pointer value domain, comparison, MOVE).

## Risks and alternatives

- **Pointer reasoning cost.** Dereferences are heavier for the solver than
  direct array access. Mitigation: keep WORKING-STORAGE *owned* (the common
  case, direct arrays); only LINKAGE/based records pay the pointer cost.
- **`byte_extract` over pointer+offset views.** Reading a sub-object slice
  through a pointer must lower correctly; validate with a focused test in
  phase 1 before relying on it for CALL.
- **Entry-point allocation lifetime.** Use static-lifetime backing objects
  (not stack `__CPROVER_allocate`) so a based record read across the whole
  analysis is well defined.
- **Alternative considered — keep copy-in/out, special-case overlap
  detection.** Rejected: it cannot model `SET ADDRESS OF` or `USAGE
  POINTER` at all, and overlap detection of arbitrary subscripted arguments
  is itself undecidable in general. The pointer model addresses all four
  features uniformly.

## Test plan

`call-alias` (two USING arguments over the same record — the callee writes
through one and reads the other), `set-address-of` (rebind a LINKAGE record
and observe through it), `address-of-pointer` (SET p TO ADDRESS OF X; SET
ADDRESS OF L TO p), and a standalone LINKAGE program (phase 1, unchanged
nondet behaviour). Each keeps the full suite green and the CardDemo clean
baseline at 44/44.
