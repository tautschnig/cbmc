# COBOL frontend for CBMC — architecture and limitations

Status: authoritative reference for the implemented state. Author: Kiro.

This document describes **what the COBOL frontend in `src/cobol/`
actually does today**: its pipeline, the key design decisions (with the
reasoning and the IBM Language Reference clauses they rest on), the
catalogue of supported features, and — precisely — every known **gap**,
**soundness issue**, and **imprecision**, each cross-referenced to its
IBM LR clause, its KNOWNBUG regression test, and the plan document that
tracks its resolution.

Companion documents:
- `cobol-frontend-plan.md` — the original engineering plan and milestone
  history (some of its §10 "known limitations" list is superseded by the
  catalogue here, which is authoritative).
- `cobol-perform-control-flow-design.md` — PERFORM / paragraph control
  flow (implemented).
- `cobol-numeric-encoding-design.md` — USAGE-faithful numeric byte
  encoding (partly implemented).
- `cobol-character-semantics-design.md` — precise character-content
  modelling (STRING/UNSTRING/INSPECT, string intrinsics, refmod,
  numeric↔alphanumeric); the Tier-2 design for I2/I3/I8/I10/I14.
- `cobol-precision-and-gaps-plan.md` — roadmap for the non-numeric
  gaps/imprecisions catalogued here.
- `cobol-differential-testing.md` — the GnuCOBOL differential-validation
  harness (`regression/cobol-diff/`) that hunts for *unknown* semantic
  divergences.

The reference throughout is the **IBM Enterprise COBOL for z/OS 6.4
Language Reference** (*LR*); supplementary references are the IBM CICS
Application Programming Reference, the Db2 for z/OS SQL Reference, and the
*z/Architecture Principles of Operation* (decimal/binary formats).

---

## 1. Scope

The frontend translates a COBOL compilation unit (`.cob`/`.cbl`) to a
CBMC GOTO program so that `cbmc program.cob` verifies user assertions and
built-in checks. Only the **source → GOTO** front end is in scope (no
property inference, no synthesis).

Dialect choices (pinned; see `cobol-frontend-plan.md` §2):

| Axis | Choice | LR basis |
|---|---|---|
| Dialect | IBM Enterprise COBOL for z/OS | reference for implementor-defined behaviour |
| Standard | COBOL-85 + common 2002/2014 scope terminators and `EXIT` forms | "Scope terminators", "EXIT statement" |
| Charset | ASCII host (EBCDIC deferred) | "USAGE DISPLAY" / collating sequence |
| Default rounding | NEAREST-AWAY-FROM-ZERO | "ROUNDED phrase" |
| Store overflow without ON SIZE ERROR | truncate (mod 10^digits) — **correct** COBOL | "SIZE ERROR phrases" |

---

## 2. Pipeline and module layout

```
.cob ─ cobol_scanner ─► tokens ─ cobol_typecheck ─► symbol table (GOTO) ─ cobol_entry_point ─► __CPROVER__start
        (fixed/free format,                (data division → byte-array
         COPY/REPLACING,                     record symbols; procedure
         bundled copybooks)                  division → one $proc function)
```

- `cobol_scanner.{cpp,h}` — normalises fixed-format (cols 1-6 seq, 7
  indicator, 8-11 Area A, 12-72 Area B; `*`/`/` comments, `-`
  continuation) and free-format source into a flat token stream;
  implements `COPY ... REPLACING` as a **source-text** operation (LR
  "COPY statement"; see the plan doc's "REPLACING is a source-text
  operation" note for why tokens carry adjacency info).
- `cobol_copybooks.{cpp,h}` — bundled library of compiler/subsystem
  copybooks (DFHAID, DFHBMSCA, the MQ `CMQ*` books) expanded at the COPY
  site through the ordinary layout path.
- `cobol_typecheck.cpp` — the bulk (~5700 lines): recursive-descent
  parser, DATA DIVISION layout, statement lowering, and GOTO code
  generation.
- `cobol_entry_point.{cpp,h}` — `__CPROVER_initialize` (VALUE init) and
  `__CPROVER__start`.
- `expr2cobol.{cpp,h}` — counterexample trace rendering.
- `cobol_language.{cpp,h}` — the `languaget` registration.

---

## 3. Key design decisions

Each decision records *what* and *why*, with the LR clause it implements.

### 3.1 Hand-written recursive-descent parser
No flex/bison. COBOL's line-oriented, reserved-word-heavy lexis and PIC
strings are awkward for a generic tokenizer and easy for a purpose-built
scanner; keeps the frontend self-contained like `ansi-c`/`cpp`.

### 3.2 Byte-addressed storage with a value-domain numeric model
Every `01`/`77`/FD record is an `array[N] of unsignedbv(8)` symbol; a
field is a `byte_extract`/`byte_update` view at its byte offset (LR
"PICTURE clause", "USAGE clause" for the sizes — `phys_size_of`). Numeric
*values* are carried in a `signedbv(64)` **scaled integer** (digits with
the implied decimal point removed; LR "PICTURE clause" `V`), so decimal
arithmetic is exact (`0.1+0.2==0.3`). The byte *sizes* follow the LR so
`REDEFINES` offsets and group MOVEs line up. The byte *content* of a
numeric is faithful only where it is observed across categories (§3.4).
Rationale: exact decimal arithmetic is the property C-lowering tools get
wrong, and most code only reads/writes numerics by value.

### 3.3 PERFORM as a re-entrant function call (not inlining)
The whole PROCEDURE DIVISION is one function `$proc(entry, exit)` holding
every paragraph as a labelled region; `GO TO` and fall-through are
ordinary branches within it; `PERFORM` is a (possibly recursive) call
(LR "PERFORM statement" return mechanism; "GO TO statement"). This makes
recursion bounded by `--unwind` rather than vacuously pruned. Fully
described and motivated (with experiments) in
`cobol-perform-control-flow-design.md`.

### 3.4 Pay-as-you-go faithful numeric encoding
`read_field`/`encode_numeric`/VALUE init dispatch on USAGE to a faithful
codec (zoned DISPLAY, packed COMP-3) **only** for fields whose bytes a
different-category item observes (a within-record REDEFINES alias),
decided by `classify_record_aliases`. All other numerics keep the fast
binary value model. Rationale: confine the codec cost and blast radius to
the fields that need byte fidelity. See
`cobol-numeric-encoding-design.md`.

### 3.5 Conditional-imperative phrases share one shape
`ON SIZE ERROR` (arithmetic), `ON OVERFLOW` (STRING/UNSTRING), `AT END`/
`INVALID KEY` (file I/O), `WHEN`/`AT END` (SEARCH) all lower to a guarded
conditional; the guard is exact when computable from the value domain
(size error) and nondeterministic when content-dependent (LR "SIZE ERROR
phrases", file statements). See `parse_io_exception`,
`parse_overflow_phrase`, `finish_arith`.

### 3.6 One operand abstraction
All operands (arithmetic, conditions, MOVE source) are parsed through one
`parse_operand`/`parse_cond_operand` that handles literals, figurative
constants, intrinsics, subscripts and reference modification, and an
operand is classified by the *result* of `parse_ref` (after refmod),
never by a pre-parse peek (LR "Reference modification" always yields an
alphanumeric result). The arithmetic "result phrase" (`receiver
[ROUNDED] ... [ON SIZE ERROR]`) is likewise shared across the five verbs.

### 3.7 Character-content verbs over a value model → havoc receivers
STRING/UNSTRING/INSPECT manipulate character content the value model does
not track, so they are modelled by parsing fully and havocking the items
they write (a sound over-approximation). This is design decision and
imprecision I10 (§5).

### 3.8 Bundled copybooks vs auto-injection
Things a program `COPY`s (DFHAID, DFHBMSCA, MQ books) come from the
bundled library; things the translator/language auto-supplies and the
program does not COPY (CICS `DFHEIBLK`, `RETURN-CODE`, `SQLCA`, IMS `DIB`)
are auto-injected at typecheck.

### 3.9 External boundaries are stubbed soundly
File I/O, SORT/MERGE, `EXEC CICS/SQL/DLI`, and `CALL` to a
separately-compiled program are abstracted with nondeterministic outputs
(§5, I11/I12/I15/I16) — the frontend cannot see into them.

---

## 4. Supported features (catalogue)

- **DATA DIVISION**: `PIC`/`USAGE` (DISPLAY, COMP/COMP-4/BINARY,
  COMP-3/PACKED-DECIMAL, COMP-5; COMP-1/2 size only — see G1); group
  items; `REDEFINES` (within a record); fixed `OCCURS` and `OCCURS …
  DEPENDING ON` (fixed at max, I7); `INDEXED BY`; level-66 `RENAMES`;
  level-88 condition-names (incl. `THRU` ranges); `VALUE` (numeric,
  alphanumeric, figurative); `SIGN` clause; qualified names.
- **PROCEDURE DIVISION**: `MOVE` (elementary numeric with scale align;
  alphanumeric pad/truncate; group/byte); `ADD`/`SUBTRACT`/`MULTIPLY`/
  `DIVIDE`/`COMPUTE` with `ROUNDED` and `ON SIZE ERROR`; `IF`/`ELSE`;
  `EVALUATE` (multi-subject `ALSO`, `THRU`, `ANY`, `WHEN OTHER`);
  `PERFORM` (out-of-line, `THRU`, sections, inline, `n TIMES` (literal or
  identifier), `UNTIL` with `TEST BEFORE/AFTER`, `VARYING … AFTER`,
  recursion bounded by `--unwind`); `GO TO` and `GO TO … DEPENDING ON`;
  `EXIT`/`EXIT PROGRAM`/`EXIT PERFORM`[`CYCLE`]/`EXIT PARAGRAPH`/`EXIT
  SECTION`; `GOBACK`/`STOP RUN`; `CONTINUE`/`NEXT SENTENCE`; `SEARCH`/
  `SEARCH ALL`; `SET` (index, condition-name TO TRUE, UP/DOWN BY);
  `INITIALIZE`; `STRING`/`UNSTRING`/`INSPECT` (havoc, I10); file I/O
  `OPEN`/`CLOSE`/`READ`[`INTO`]/`WRITE`[`FROM`]/`REWRITE`/`DELETE`/`START`
  (I11); `SORT`/`MERGE`/`RELEASE`/`RETURN` (I12); `ACCEPT` (I13);
  `DISPLAY` (no-op); `CALL` (stub, I16); `EXEC CICS/SQL/DLI` (stub, I15).
- **Conditions**: relational (symbol and word operators, `IS`/`NOT`);
  class `IS [NOT] NUMERIC/ALPHABETIC[-UPPER|-LOWER]` (exact for
  alphanumeric operands; nondet for numeric, I5); sign `IS [NOT]
  POSITIVE/NEGATIVE/ZERO` (exact); condition-names (88); abbreviated
  combined conditions.
- **Intrinsics**: exact — `LENGTH`, `NUMVAL`/`NUMVAL-C`, `UPPER-CASE`/
  `LOWER-CASE`/`REVERSE` (on literals), and the `numeric_exact` set
  (`ABS`/`MAX`/`MIN`/`SUM`/`MOD`/`REM`/`INTEGER`/`INTEGER-PART`/…); all
  others nondet (I14).
- **Verification primitives**: `CALL "__CPROVER_assert"`/`__CPROVER_assume"`.

The full AWS CardDemo corpus (44 programs) parses and lowers with no
conversion errors; all 44 reach `VERIFICATION SUCCESSFUL` with
`--unwind 3 --no-unwinding-assertions`.

---

## 5. Catalogue of gaps, soundness issues, and imprecisions

Legend: **G** gap (unsupported/partial), **S** soundness (verdict could
be wrong), **I** imprecision (sound over-approximation; cannot make a
false assertion pass, but may prevent proving a true one). "KB" names the
`regression/cobol/knownbug-*` test (run with `test.pl -K`).

### Implicit runtime-property checks (verification-tool contract)

Beyond user assertions, the frontend instruments COBOL's own runtime-fault
conditions — the analogue of CBMC's built-in C checks. CBMC's C-level
`--bounds-check`/`--div-by-zero-check` do **not** catch these on the
byte-array / value-domain lowering (confirmed by experiment), so they are
emitted by the frontend. See doc/architectural/cobol-runtime-checks.md.

| class | checks | status | LR clause |
|---|---|---|---|
| `cobol:subscript-range` | `1 <= subscript <= occurs` per dimension | **DONE** (CORE `subscript-range-ok`/`-bad`) | "Subscripting"; SSRANGE |
| `cobol:refmod-range` | `start >= 1` and `start+length-1 <= size` | **DONE** (CORE `refmod-range-ok`/`-bad`, `refmod-loop-var`) | "Reference modification"; SSRANGE |
| `cobol:division-by-zero` | divisor non-zero (absent `ON SIZE ERROR`) | **DONE** (CORE `division-by-zero-bad`, `division-size-error`) | "DIVIDE"/"COMPUTE"; SIZE ERROR |
| `cobol:numeric` (S0C7) | numeric operand holds valid digits | **DONE, opt-in** `--cobol-data-exception-check` (CORE `data-exception-bad`/`-guard`) | "Class condition"; data exception |

Gated on the `bounds-check` option (on by default in CBMC v6+; off under
`--no-standard-checks`). On CardDemo (`--unwind 3`) they flag four genuine
potential overruns (unvalidated external/DB2 lengths and a commarea page
index); `--no-standard-checks` reproduces the clean 44/44 user-assertion
baseline.

### Soundness

| id | issue | LR clause | KB | plan |
|---|---|---|---|---|
| S1 | **RESOLVED** — unsigned `PIC 9` now stores the absolute value (sign dropped on store); was modelled in a signed domain | "MOVE statement"; "PICTURE clause" (9) | CORE `unsigned-sign` | — |
| S2 | **RESOLVED** — `COMP`/`BINARY` byte-aliased fields are now stored and read big-endian two's complement (z/Architecture), via `encode_binary`/`decode_binary` (the codec owns byte order; all store paths consistent) | "USAGE clause" (BINARY); z/Arch PoO | CORE `comp-endian-binary` | numeric-encoding §3.2, §5 |
| S3 | **RESOLVED** — a 01-level `REDEFINES` now shares the redefined record's storage (aliases it) | "REDEFINES clause" | CORE `redefines-01-alias` | — |
| S4 | EBCDIC not implemented (ASCII host): collating sequence and zoned/sign codecs differ from z/OS | "USAGE DISPLAY"; collating sequence | — | numeric-encoding §5 (charset) |

### Gaps

| id | gap | LR clause | KB | plan |
|---|---|---|---|---|
| G1 | **RESOLVED** — `COMP-1`/`COMP-2` are numeric IEEE single/double items; `valuet` carries an `is_float` flag (double domain), with read/store, fixed↔float MOVE conversion, float arithmetic (verbs + COMPUTE) and comparison (CORE `comp2-float`, `comp2-arith`, `comp1-compare`) | "USAGE clause" (COMP-1/2) | — | precision-and-gaps §6 |
| G2 | EBCDIC charset (see S4) | "USAGE DISPLAY" | — | numeric-encoding §5 |
| G3 | `ALTER` unsupported | "ALTER statement" (obsolete) | — | precision-and-gaps §4 |

### Imprecision (sound; nondeterministic / over-approximate)

| id | imprecision | LR clause | KB | plan |
|---|---|---|---|---|
| I2 | **RESOLVED for items** — a bare numeric item operand (any USAGE, scale, sign) is compared by its magnitude digit-string display representation (CORE `num-alnum-compare`, `num-alnum-compare-ext`); only an arithmetic-*expression* operand stays nondet | "Comparison of numeric and nonnumeric operands" | — | character-semantics §3.6 |
| I3 | **RESOLVED** — MOVE alphanumeric → numeric now de-edits the digit characters (CORE `move-alnum-to-num`) | "MOVE statement" | — | — |
| I4 | **PARTIAL** — MOVE to a numeric-edited PICTURE now applies editing (Z/`*` suppression, `.`, `,`, B/0//; CORE `move-edited`); floating/fixed sign and currency (+ - $ CR DB) still nondet | "MOVE statement"; "PICTURE clause" editing | — | numeric-encoding |
| I5 | **RESOLVED** — IS NUMERIC on a numeric operand now evaluates `numeric_content_valid` over the item's faithful bytes (digit/sign validity for zoned/packed; trivially true for the value model / BINARY) | "Class condition" | CORE `class-numeric`, `class-numeric-redefine` | numeric-encoding |
| I6 | **RESOLVED** — group MOVE now space-pads a longer receiver on the right | "MOVE statement" (group) | CORE `group-move-pad` | — |
| I7 | `OCCURS … DEPENDING ON` fixed at maximum (over-approx) | "OCCURS clause" (format 2) | — | precision-and-gaps §2 |
| I8 | **RESOLVED** — a non-constant reference-modification length is carried as `dyn_size`; all byte-string consumers honour it (MOVE sender + receiver, alphanumeric comparison, STRING/UNSTRING) via a per-byte runtime guard, CORE `refmod-dynamic-{length,compare,string,unstring,receiver}` | "Reference modification" | — | precision-and-gaps §6 |
| I9 | ambiguous qualified reference takes the first match (with a warning) | "Qualification" | — | precision-and-gaps §2 |
| I10 | **RESOLVED** — INSPECT TALLYING/REPLACING (incl. BEFORE/AFTER INITIAL), STRING (SIZE and delimiter) and UNSTRING (split with OR/ALL) are modelled exactly for the common cases over the item bytes; a non-modellable form (e.g. a multi-byte delimiter in an OR/ALL set) still havocs, soundly | "STRING"/"UNSTRING"/"INSPECT statement" | — | character-semantics |
| I11 | file I/O external: OPEN/CLOSE no-op, READ havocs the record + nondet AT END, output verbs nondet INVALID KEY | "READ"/"WRITE"/… statements | — | precision-and-gaps §3 |
| I12 | SORT/MERGE abstracted (procedures performed; RETURN havocs) | "SORT"/"MERGE statement" | — | precision-and-gaps §3 |
| I13 | ACCEPT havocs its receiver (run-time input unknown) — **by design** | "ACCEPT statement" | — | precision-and-gaps §3 |
| I14 | **PARTIAL** — `UPPER-CASE`/`LOWER-CASE`/`REVERSE` now exact on item arguments too (CORE `string-intrinsics-item`); other intrinsics still nondet on items | "Intrinsic functions" | — | character-semantics §3.4 |
| I15 | `EXEC CICS/SQL/DLI` stubbed (outputs + status nondet) | LR + CICS/Db2 refs | — | precision-and-gaps §3 |
| I16 | **PARTIAL** — a `CALL "literal"` to any program in the same file (forward or backward) is linked via a two-pass signature collection: copy-in / call / copy-out of USING args and RETURNING (CORE `call-linkage`, `call-by-content`, `call-forward`, `call-returning`). Dynamic/external calls still use the sound havoc stub; argument aliasing needs the pointer model (cobol-pointer-model.md) | "CALL statement" | — | cobol-call-linkage.md |
| I17 | `SET` condition-name `TO FALSE` is a no-op (no FALSE clause modelled) | "SET statement" (format 5) | — | precision-and-gaps §4 |
| I18 | `SET ADDRESS OF` / pointer forms are no-ops (no pointer model); design scoped, see cobol-pointer-model.md | "SET statement" (pointer) | — | cobol-pointer-model.md |
| I20 | uninitialised (no-VALUE) reads are not flagged (nondet, sound) | — | — | precision-and-gaps §2 |

### Correct-but-worth-noting (not bugs)

- Store overflow without `ON SIZE ERROR` truncates `mod 10^digits` — this
  is the LR-specified behaviour ("SIZE ERROR phrases"), confirmed by test.
- `DISPLAY` is a no-op (no console model); its operands are still read so
  they are not sliced away.
- A recursive `PERFORM` needs an explicit `--unwind` bound — inherent to
  bounded model checking, not a defect (the soundness fix is that it is
  bounded, not vacuously pruned; see the PERFORM design doc).

---

## 6. How to run the KNOWNBUG tests

The KNOWNBUG tests above are excluded from the default CORE regression run
and document current divergences (they encode the *correct* LR behaviour
and currently fail). Run them with:

```sh
cd regression/cobol && ../test.pl -K -c <path-to>/cbmc
```

When a gap/imprecision is closed, the corresponding test is promoted from
`KNOWNBUG` to `CORE`.
