# COBOL frontend for CBMC — implementation plan

This document is the engineering plan for a COBOL language frontend in
CBMC. It captures scope, architecture, the type- and statement-lowering
decisions, and the milestone order. It is the spec the implementation in
`src/cobol/` is built against.

Background reading this plan depends on:

- `doc/architectural/cbmc-frontend-architecture.md` — the `languaget`
  interface and the conversion pipeline.
- `doc/architectural/building-a-new-frontend.md` — the step-by-step
  recipe and the testing methodology.
- The `Cobol2Goto` design notes (`approach.md`, `cobol-semantics.md`,
  `cobol-to-goto-lowering.md`) — what COBOL means and how to lower it.

We are building *only* the frontend (source → GOTO). The wider
"verification-driven migration" pipeline (property inference, Java
synthesis, differential validation) described in the Cobol2Goto README
is explicitly out of scope.

---

## 1. Goal and definition of done

A COBOL compilation unit (`.cob` / `.cbl`) can be handed to `cbmc`
directly, and CBMC verifies user assertions and built-in checks against
it. Concretely, for the day-one subset:

```sh
cbmc program.cob
```

parses, type-checks, lowers to GOTO, runs symbolic execution, and
reports `VERIFICATION SUCCESSFUL` / `FAILED` with assertions that are
*actually checked* (not silently dropped). A regression suite under
`regression/cobol/` exercises each supported construct with positive and
negative cases.

---

## 2. Dialect, standard, charset

Fixed choices for the first cut (matching `approach.md`'s
recommendation that you verify a *dialect*, not "COBOL"):

| Axis | Choice |
|---|---|
| Dialect | IBM Enterprise COBOL (reference for implementor-defined behaviour) |
| Standard | COBOL-85 + common 2002/2014 scope terminators (`END-IF`, `END-PERFORM`, ...) |
| Charset | ASCII (host charset; EBCDIC sign/zone codecs deferred) |
| Default rounding | `NEAREST-AWAY-FROM-ZERO` (`ROUNDED` without `MODE`) |
| Arithmetic overflow w/o `ON SIZE ERROR` | `ASSERT` (flag as a property), matching the doc's "treat UB as an assertion" guidance |

These are recorded here so the semantics are pinned; a future
`--dialect` flag can override.

---

## 3. Parser strategy

**Hand-written recursive-descent parser, built in.** No external
process, no flex/bison for the first cut.

Rationale:

- The day-one subset is small and regular; COBOL's lexical structure
  (line-oriented, reserved-word heavy, `PIC` strings) is awkward for a
  generic flex tokenizer but easy for a purpose-built scanner.
- A built-in parser keeps the frontend self-contained — no Java/OCaml
  runtime dependency (ProLeap / SuperBOL), matching how `ansi-c`,
  `cpp`, and `statement-list` are built in-tree.
- We fully control error recovery and source locations.

The scanner normalises **both fixed-format** (cols 1–6 sequence, col 7
indicator, 8–11 Area A, 12–72 Area B; `*`/`/` comments, `-`
continuation) **and free-format** source into a flat token stream
tagged with source locations. Reserved words are matched
case-insensitively. The parser is a set of mutually-recursive
`parse_*` methods producing a typed parse tree
(`cobol_parse_treet`).

The AST is custom C++ structs (like `ansi_c_parse_treet`), not a
generic `irept`, for clarity.

---

## 4. Type lowering (`PIC` + `USAGE` → CBMC types)

Following `cobol-to-goto-lowering.md` §1 but with a documented
simplification for the MVP.

### 4.1 Numerics — value-domain model

Every numeric elementary item carries `(digits, scale, signed, usage)`.
We model the **logical value** as a `signedbv_typet(W)` (or
`unsignedbv_typet(W)` when unsigned), holding the value as an **integer
in units of 10^-scale** — i.e. the digits with the implied decimal
point removed. `PIC 9(5)V99` holding `1234.56` is the integer
`123456` at scale 2. This makes decimal arithmetic exact
(`0.1 + 0.2 == 0.3` because `1 + 2 == 3` at scale 1), which is the whole
point of COBOL numerics and the thing C-lowering tools get wrong.

Width `W` is chosen wide enough to hold `digits` decimal digits with
headroom:

| digits | width |
|---|---|
| ≤ 4 | 32 |
| ≤ 9 | 64 |
| ≤ 18 | 128 |

We deliberately use generous widths so intermediate results don't
overflow the IR type; picture-range overflow is enforced explicitly by
`ON SIZE ERROR` checks at stores, not by the bitvector width.

**Simplification (documented limitation):** the MVP models numerics by
*value*, not by *bytes*. This means byte-level `REDEFINES` overlap of a
numeric field, group `MOVE` spanning a numeric field, and `COMP-3`
sign-nibble bit-twiddling are **not** byte-accurate yet. The byte-array
storage model (`cobol-to-goto-lowering.md` §1.5) is the planned
follow-up; until then a `REDEFINES` that reinterprets numeric bytes is
flagged, not silently mis-modelled.

### 4.2 Alphanumeric — byte arrays

`PIC X(n)` / `PIC A(n)` → `array_typet(unsignedbv_typet(8), n)`. Group
`MOVE` and `MOVE` of alphanumerics are byte copies with space padding.

### 4.3 Group items (records)

A group (a level with subordinates) → `struct_typet` whose components
mirror the elementary subordinates in declaration order (recursively).
`01` and `77` items are top-level symbols.

### 4.4 `OCCURS`

Fixed `OCCURS n TIMES` → `array_typet(elem, n)`. Subscripts are
1-based in COBOL, 0-based in the IR — the converter subtracts 1 and
emits a bounds `ASSERT`. `OCCURS DEPENDING ON` is deferred
(worst-case-allocation is the planned approach).

### 4.5 Condition names (88-levels)

Recorded as derived predicates over the parent item; `IF cond-name`
expands to the disjunction of the parent equalling each `VALUE`
(supporting `THRU` ranges). `SET cond-name TO TRUE` assigns the first
`VALUE`.

### 4.6 USAGE handling in the MVP

`DISPLAY`, `COMP`/`COMP-4`/`BINARY`, `COMP-3`/`PACKED-DECIMAL`,
`COMP-5` all collapse to the same value-domain integer in the MVP
(they differ only in byte encoding, which the value model abstracts
away). `COMP-1`/`COMP-2` (float) are deferred. This is sound for
value-level reasoning and is the documented MVP boundary.

---

## 5. WORKING-STORAGE and symbols

- `WORKING-STORAGE` and `FILE SECTION` record items → **static-lifetime
  symbols**, no `DECL`/`DEAD`. Initialised from `VALUE` clauses by
  `__CPROVER_initialize` (built in `generate_support_functions`, exactly
  like the statement-list entry point).
- Items with no `VALUE` are left nondeterministic (uninitialised reads
  are a documented UB site; flagging them is a future check).
- `LINKAGE SECTION` → parameters (deferred until `CALL` lands).
- Fully-qualified symbol names: `cobol::<PROGRAM-ID>::<item>`.
  Frontend-introduced temporaries are prefixed `__cobol_`.

---

## 6. Statement / control-flow lowering

Per `cobol-to-goto-lowering.md` §2–§3. The whole `PROCEDURE DIVISION`
of one `PROGRAM-ID` becomes **one goto-function**; paragraphs are
labelled regions emitted in source order; fall-through is preserved.

Day-one statements:

- `MOVE` (elementary numeric with scale alignment; alphanumeric with
  pad/truncate; group as byte/value copy).
- `ADD`/`SUBTRACT`/`MULTIPLY`/`DIVIDE`/`COMPUTE`, with `ROUNDED` and
  optional `ON SIZE ERROR` / `NOT ON SIZE ERROR`.
- `IF` / `ELSE` / `END-IF`; conditions with relational, `AND`/`OR`/`NOT`,
  and condition-names.
- `EVALUATE` (chained-guard lowering, including `EVALUATE TRUE`,
  `WHEN`, `WHEN OTHER`, `THRU`, `ANY`).
- `PERFORM`: out-of-line (single + `THRU`), `n TIMES`, `UNTIL`
  (`TEST BEFORE`/`AFTER`), `VARYING ... [AFTER ...]`, and inline
  `PERFORM ... END-PERFORM`. `THRU` and nested/overlapping ranges use
  the **perform-stack** scheme (`§3.5`).
- `GO TO` and `GO TO ... DEPENDING ON` (computed goto, out-of-range
  falls through).
- `GOBACK` / `EXIT PROGRAM` / `STOP RUN` (returns; `STOP RUN` as a
  non-returning terminator via `assert`/`assume false` stub).
- `DISPLAY` / `ACCEPT` — stubbed (DISPLAY reads its operands so they
  aren't sliced away; no observable effect modelled).
- `CONTINUE` / `NEXT SENTENCE` — skip.
- Verification primitives: `CALL "__CPROVER_assert" USING cond msg`,
  `CALL "__CPROVER_assume" USING cond`. (COBOL has no `assert`; we
  expose CPROVER intrinsics via the static-`CALL` name convention so
  tests can state properties.)

Deferred / refused (diagnostic, not silent): `ALTER`, dynamic `CALL`,
`EXEC SQL`/`EXEC CICS`, file I/O state machine, `SORT`/`MERGE`,
`REPORT SECTION`, `OCCURS DEPENDING ON`, byte-level `REDEFINES`.

---

## 7. Entry point

`generate_support_functions` mirrors `statement_list_entry_point`:

1. Pick the main program (`config.main` if set, else the single/first
   `PROGRAM-ID`).
2. Build `__CPROVER_initialize` assigning every static-lifetime symbol
   its `VALUE`-derived initial value.
3. Build `__CPROVER__start` (`goto_functionst::entry_point()`) that
   calls `__CPROVER_initialize` then the main program function.

---

## 8. Module / directory layout

```
src/cobol/
├── CMakeLists.txt          # file(GLOB sources); add_library(cobol ...)
├── Makefile                # SRC = ... ; include ../common
├── module_dependencies.txt # goto-programs langapi linking util
├── cobol_language.{h,cpp}  # languaget implementation + new_cobol_language()
├── cobol_parse_tree.{h,cpp}# AST structs
├── cobol_parser.{h,cpp}    # scanner + recursive-descent parser
├── cobol_types.{h,cpp}     # PIC/USAGE model + lowering to typet
├── cobol_typecheck.{h,cpp} # parse tree → symbol table (data + procedure)
├── cobol_convert.{h,cpp}   # statement/expression → codet (the converter)
├── cobol_entry_point.{h,cpp}
└── expr2cobol.{h,cpp}      # from_expr/from_type for counterexample traces
```

Registration: `register_language(new_cobol_language)` added to
`src/cbmc/cbmc_languages.cpp` and the other tools' `*_languages.cpp`
(`goto-cc`, `goto-instrument`, `goto-analyzer`, `goto-diff`,
`goto-synthesizer`). Build wiring: `add_subdirectory(cobol)` in
`src/CMakeLists.txt`; `cobol` added to `src/Makefile` `DIRS` and the
`languages` target.

---

## 9. Milestones

1. **Skeleton + wiring**: directory, build files, a `languaget` that
   parses to an empty parse tree and produces an empty symbol table;
   registered and linking into `cbmc`. (Build green.)
2. **Trivial verify**: a program with WORKING-STORAGE integers and a
   `__CPROVER_assert` call verifies (SUCCESS and FAILED both observable).
3. **Arithmetic + MOVE + IF**: integer then decimal-scale arithmetic;
   `MOVE`; `IF`/`ELSE`; relational/logical conditions.
4. **PERFORM family + paragraphs + GO TO**: perform-stack, loops.
5. **EVALUATE, condition-names, OCCURS (fixed)**.
6. **Traces**: `expr2cobol` so counterexamples read in COBOL syntax.
7. **Hardening**: per-construct regression tests, negative cases,
   confirm assertions are actually checked (grep `assertion`), document
   limitations.

Each milestone is a logical commit; commits stay `git-clang-format`
clean and use curly-brace constructor syntax.

---

## 10. Known limitations (tracked, to be kept current)

- **Storage is byte-addressed** (records are byte arrays; fields are
  `byte_extract`/`byte_update` views), so `REDEFINES`, group items, and
  fixed `OCCURS` are modelled soundly. However the *physical encoding*
  is a uniform little-endian two's-complement binary, **not** real
  zoned-decimal / packed-decimal / EBCDIC. The byte *sizes* follow the
  IBM LR (so `REDEFINES` offsets line up), but code that inspects the
  actual zoned/packed bytes will not see IBM-faithful content. (See
  `phys_size_of`, IBM LR pp. 9048-9075.)
- Group `MOVE` copies `min(sizes)` bytes with no space/zero padding of a
  longer receiver.
- Subscripting a subfield *inside* an `OCCURS` group (`FIELD(I)` where
  `FIELD` is subordinate to the table) is not supported — only the
  table item itself is subscriptable.
- Alphanumeric relational comparisons in conditions (`IF X = "Y"`,
  `IF X = SPACES`) are not yet supported in expressions; 88-level
  condition names over alphanumeric parents are.
- Qualified references (`FIELD OF GROUP` / `IN`) are not yet resolved.
- Edited PICTUREs (insertion/suppression characters `Z * . , + - $ CR
  DB /`) are not parsed; the embedded `.` currently mis-tokenises.
- EBCDIC and sign-nibble/zone codecs not implemented (ASCII host only).
- Float (`COMP-1`/`COMP-2`), `OCCURS DEPENDING ON`, files, `EXEC`
  sub-languages, `SORT`/`MERGE`, dynamic `CALL`, `ALTER` unsupported.
- `SET ... TO FALSE` and pointer/`ADDRESS OF` forms are no-ops.
- Single compilation unit focus; multi-program `CALL` linkage is a
  follow-up.

---

## 11. Baseline against AWS CardDemo and progress

We track progress against the AWS CardDemo corpus (44 real COBOL
programs, heavy on CICS/VSAM/DB2/copybooks) by recording the *first*
construct that blocks each program. This is a coverage signal, not a
verification target — most of these programs ultimately need `EXEC
CICS`/`COPY`/file modelling to verify.

Progression of the dominant first-blocker (programs affected):

| Blocker | Initial | alnum VALUE | OCCURS+SET | COPY | byte model |
|---|---|---|---|---|---|
| alphanumeric / figurative `VALUE` | ~26 | 0 | 0 | 0 | 0 |
| `OCCURS` | 9 | 13 | 0 | 0 | 0 |
| digit paragraph / `VALUES` | 10 | 0 | 0 | 0 | 0 |
| `REDEFINES` | — | 13 | 19 | 32 | **0** |
| unknown data item (`COPY` books) | — | 1 | 15 | 2 | **19** |
| non-numeric item in expression | — | — | — | — | **14** |
| edited PICTUREs / misc | — | ~4 | ~10 | ~10 | ~9 |

After the byte-level storage model, every program parses its entire
DATA DIVISION (including `REDEFINES`, group items and fixed `OCCURS`)
and fails only on PROCEDURE-level constructs. The next high-impact
items, in order, are now:

1. **Alphanumeric comparisons** in conditions (`IF X = "Y"`,
   `IF X = SPACES`) — needed by most procedure code (14 programs).
2. **Qualified references** (`FIELD OF GROUP`) and subscripting of
   subfields inside `OCCURS` groups — the "unknown data item" /
   "subscript on a non-table item" failures.
3. **Edited PICTUREs** — scanner-level PIC handling (the `.` mis-token).
4. **`EXEC CICS` / `EXEC SQL`** stubbing (treat as nondet I/O) — the
   ultimate gate for the CICS programs.

Registering the frontend with the other tools (`goto-cc`,
`goto-instrument`, …) remains a small follow-up.
