# Plan: precision improvements and gap closure (non-numeric)

Status: living plan. Author: Kiro.

This document is the roadmap for the gaps, soundness issues, and
imprecisions of the COBOL frontend that are **not** about numeric byte
encoding (those are in `cobol-numeric-encoding-design.md`) or PERFORM
control flow (`cobol-perform-control-flow-design.md`). It is referenced,
item by item, from `cobol-frontend-architecture.md`, which is the
authoritative catalogue.

Each item is grounded in the IBM Enterprise COBOL for z/OS 6.4 Language
Reference (*LR*). Items have a stable id (I*/G* matching the architecture
catalogue) and, where demonstrable, a KNOWNBUG regression test under
`regression/cobol/knownbug-*`.

Classification:
- **Imprecision** — a sound over-approximation (typically a
  nondeterministic value); it can prevent a *true* assertion from being
  proved, but never makes a false assertion pass. Safe; reduces utility.
- **Gap** — a construct that is unsupported or only partially parsed.
- **Soundness** — the model can diverge from real COBOL such that a
  verdict could be wrong; see the numeric/PERFORM docs for those.

---

## 1. Character-content verbs (the value model has no character semantics)

The storage is byte-addressed but values are kept in a numeric value
domain, so the *character content* that some verbs manipulate is not
tracked. The shared treatment is to **havoc the receivers** (a sound
over-approximation), which is why the following are imprecise. The
precise design for all of these — and why no new representation is needed
(the bytes are already there) — is
`cobol-character-semantics-design.md`; the notes below summarise.

### I10 — STRING / UNSTRING / INSPECT (KNOWNBUG: string-precise, inspect-precise)
LR "STRING statement", "UNSTRING statement", "INSPECT statement".
Current: receivers (STRING/UNSTRING targets, INSPECT TALLYING counters,
INSPECT REPLACING/CONVERTING targets) are assigned nondeterministic
values; the `ON OVERFLOW` phrase is a nondet guard.
Plan: introduce an explicit **character/byte interpretation** carried
alongside the numeric value (the architectural change flagged in the plan
doc's "character-level verbs" note), then implement each verb's
concatenation / split / count / replace over the bytes. Large; do
INSPECT TALLYING (countable) first, then STRING (concatenation), then
UNSTRING. Requires the faithful byte encodings (numeric doc) for numeric
operands of these verbs.

### I14 — intrinsic functions on item arguments (KNOWNBUG: intrinsic-item)
LR "Intrinsic functions". Current: exact only for `LENGTH`, the
`numeric_exact` set (ABS/MAX/MIN/SUM/MOD/REM/INTEGER/INTEGER-PART/...),
and `NUMVAL`/`NUMVAL-C`/`UPPER-CASE`/`LOWER-CASE`/`REVERSE` on **string
literals**; every other intrinsic, and the string intrinsics on an
**item** argument, return a nondeterministic value of the right category.
Plan: once a character interpretation exists (I10), implement
`UPPER-CASE`/`LOWER-CASE`/`REVERSE`/`TRIM`/`ORD`/`CHAR` exactly on items;
keep genuinely environmental ones (`CURRENT-DATE`, `RANDOM`, `WHEN-
COMPILED`) nondet by design.

---

## 2. Data description and references

### I6 — group MOVE has no padding (KNOWNBUG: group-move-pad)
LR "MOVE statement" (group / alphanumeric receiver: the receiver is
filled and space-padded on the right). Current: a group/byte MOVE copies
`min(source,receiver)` bytes; a longer receiver keeps its old trailing
bytes. Plan: in `make_move_group`, when the receiver is longer, fill the
remainder with `0x40`/space for an alphanumeric receiver (and `0x00` for
a numeric one) — a localised fix; no architectural change needed.

### I7 — OCCURS DEPENDING ON is fixed at the maximum
LR "OCCURS clause", format 2. Current: a variable table is allocated at
`integer-2` (max) occurrences — a sound over-approximation; the DEPENDING
ON object is an ordinary numeric item the program maintains. Plan:
constrain table-element reads/writes and group length by the current
DEPENDING ON value (assume `1 <= odo <= max`); model the group's length
as `base + odo*stride`. Medium; interacts with subscript bounds checks.

### I8 — reference modification with a non-constant length
LR "Reference modification". Current: `id(start:len)` with a non-constant
`len` is over-approximated to the item's remaining size. Plan: model the
exact `len` (bounds-check `start>=1`, `start+len-1<=size`) and produce a
length-`len` view; requires variable-length alphanumeric values, which
ties into the character interpretation (I10).

### I9 — ambiguous qualified reference takes the first match
LR "Qualification" (a reference must be made unique). Current: when
qualifiers do not disambiguate, the first matching item is used with a
warning. Plan: this is a (rare) program error; promote the warning to an
error, or keep first-match with the warning. Low priority.

### I20 — uninitialised reads are not flagged
Current: an item with no VALUE is left nondeterministic (CBMC
zero-initialises the record symbol), and reading it before a write is not
reported. Plan: add an optional "read-before-write" check
(`--cobol-uninitialized`); low priority, sound as-is.

---

## 3. External interfaces (inherently abstracted)

These model boundaries the frontend cannot see into; the abstractions are
sound over-approximations and, for the most part, *by design* rather than
fixable bugs. They are listed so the abstraction is explicit.

### I11 — file I/O is external (KNOWNBUG: none — inherent)
LR "READ"/"WRITE"/"REWRITE"/"DELETE"/"START"/"OPEN"/"CLOSE". Current:
OPEN/CLOSE are no-ops; READ havocs the FD's record area (and copies it to
an INTO receiver) with a nondet `AT END`; the output verbs model only the
nondet `INVALID KEY`/`AT END` outcome (`parse_io_exception`). Plan: an
optional in-memory file model (a sequence of records with a position)
would make sequential read/write precise within a harness; medium, and
only useful with a test harness that supplies file contents.

### I12 — SORT / MERGE are abstracted
LR "SORT statement", "MERGE statement". Current: INPUT/OUTPUT PROCEDUREs
are PERFORMed (so their logic is analysed); the sort itself is not
modelled and RETURN havocs the record. Plan: with the file model (I11), a
SORT could be modelled as "the output is some permutation of the input";
generally low value for assertion checking.

### I13 — ACCEPT havocs its receiver
LR "ACCEPT statement". Current: the receiver is havoced (date/time/
operator input is unknown at verification time). This is correct and
**by design**; listed for completeness.

### I15 — EXEC CICS / SQL / DLI are stubbed
LR + CICS Application Programming Reference + Db2 SQL Reference. Current:
output operands and per-command status fields (EIB, SQLCA) are havoced; a
nondet EXEC INTERFACE BLOCK is synthesised; RETURN/XCTL terminate. Plan:
model specific high-value commands (e.g. `EXEC CICS READ` into a working
record) under a harness; large, subsystem-specific.

### I16 — CALL is stubbed
LR "CALL statement". Current: a called program is not linked; its `BY
REFERENCE`/`RETURNING` receivers are havoced, `BY CONTENT`/`BY VALUE`
arguments are ignored, no inter-program linkage. Plan: when the called
program is in the same compilation set, link and pass arguments
(inter-program analysis); medium-large.

---

## 4. Statement gaps

### I17 — SET condition-name TO FALSE is a no-op
LR "SET statement" (format 5) / the `FALSE` clause. Current: without a
`WHEN SET TO FALSE` value the result is implementor-defined; modelled as a
no-op. Plan: when the 88-level has a `FALSE` literal, assign it; otherwise
keep the no-op. Small.

### I18 — SET ADDRESS OF / pointer forms are no-ops
LR "SET statement" (formats 1/6, pointer data). Current: pointer/ADDRESS
forms are no-ops (the frontend has no pointer/based-storage model). Plan:
model `LINKAGE` based addressing only if a real program needs it; low
priority.

### G3 — ALTER is unsupported
LR "ALTER statement" (an obsolete element). Current: not recognised.
Plan: refuse with a clear diagnostic; not planned to implement (obsolete,
absent from the corpus).

---

## 5. Priority order

1. **I6 group-move padding** — small, localised, removes a common false
   "cannot prove" on record initialisation.
2. **Character interpretation** (enables I10 STRING/INSPECT precision and
   I14 string intrinsics on items, and feeds I8) — the one architectural
   investment with the widest precision payoff.
3. **I7 OCCURS DEPENDING ON** — medium; improves table-bound precision.
4. The external-interface items (I11/I12/I15/I16) are harness-dependent
   and lower priority for pure front-end precision.

---

## 6. Remaining architectural items (warrant a design, not a local fix)

These three are not local fixes: each needs a new capability in a core
abstraction, so they are scoped here as dedicated increments.

### I8 — reference modification with a non-constant length

Root cause (verified): an item view carries a **static** `byte_size`, and
every consumer (`make_move_group`'s fixed-size byte copy, STRING/UNSTRING,
the alphanumeric comparison's per-byte unfolding) reads that many bytes at
compile time. A non-constant `X(start:len)` has a runtime byte count, so
`apply_refmod` over-approximates `length` to the item size — which is both
imprecise and, because the advanced offset plus the full size can exceed
the item, a latent over-read.

Architectural change: give a reference-modified view an optional
**dynamic length** `exprt` and make the byte-string consumers honour it:
a length-bounded copy/compare (a loop bounded by `len`, or a masked
`byte_update`), with the static `byte_size` kept only as an upper bound for
storage. This is the same "dynamic-extent operand" capability OCCURS
DEPENDING ON (I7) needs, so the two should share one mechanism.

**Status (resolved).** `reft` (and `cond_operandt`) carry an optional
`dyn_size` expr, set by `apply_refmod` for a non-constant length (the
static `byte_size` becomes an upper bound = bytes from `start` to the item
end). Every byte-string consumer honours it with the same per-byte-guard
pattern (unfold over the static upper bound, guard position `i` by
`i < dyn_size`, else a space pad / the receiver's current byte): the
**MOVE sender** and **receiver** (`make_move_group` / the literal-receiver
path — a `Y(s:n)` receiver writes only its `n`-byte window, leaving the
rest of `Y` unchanged), the **alphanumeric comparison**
(`build_alnum_relation`), and **STRING/UNSTRING** (a STRING sender
contributes `len` characters; UNSTRING bounds its source scan by `len`,
which was a one-line change because the source length was already a single
`s_expr`). IBM LR "Reference modification", "MOVE statement", "Comparison
of two alphanumeric operands", "STRING statement", "UNSTRING statement".

Pitfall: a default-constructed `exprt` has an empty id (not `nil`), so
`dyn_size` must be initialised to `nil_exprt{}` and tested with
`is_not_nil()` — otherwise every static view wrongly takes the dynamic
path.

### I16 / CALL — inter-program linkage for whole-program verification

Current: each `PROGRAM-ID` is verified standalone; `CALL` havocs its
`BY REFERENCE`/`RETURNING` arguments (sound). Whole-program verification
needs: link multiple programs into one symbol table; bind the caller's
`USING` arguments to the callee's `LINKAGE SECTION` items as **aliases**
(BY REFERENCE) or copies (BY CONTENT/VALUE) — IBM LR "CALL statement" /
"Linkage"; and lower a `CALL` to an actual call of the callee's
function. The hard part is aliasing a callee LINKAGE record onto the
caller's storage (our records are distinct byte-array symbols); it likely
needs the callee body re-expressed against the caller's record exprs, or a
pointer/based-storage model (shared with I18 SET ADDRESS OF). A large,
design-first effort; standalone analysis stays the default.

### G1 — COMP-1 / COMP-2 IEEE floating point (resolved)

`valuet` now carries an `is_float` flag: a float value has IEEE `double`
type and the `scale` is unused, while fixed-point keeps the scaled-integer
model. COMP-1/COMP-2 are recognised as numeric items (4/8 bytes, not
groups). The reconciliation of mixed operands is centralised: `to_float`
converts a fixed value `v` to `(double)v / 10^scale`, and `try_float_arith`
(used by `vadd`/`vsub`/`vmul`, `divide_values`, and the expression parser)
promotes both operands to double and emits an `ieee_float_op_exprt` when
either is float — so the verbs and COMPUTE share one fixed/float decision
point. `read_field` reads a float item at its IEEE type (widening COMP-1 to
double); `encode_numeric` narrows on store and converts a float source to a
fixed receiver by truncation toward zero (IBM LR "MOVE statement"); float
comparison uses `ieee_float_equal`/`notequal` and ordering relations.
Rounding mode is round-to-nearest-even for arithmetic, round-to-zero for
the float→fixed truncation. A COMP-1/COMP-2 `VALUE` initialises the item to
the literal's IEEE bytes (`float_value_bytes`). Remaining: floating-point
literals in `E` notation.

