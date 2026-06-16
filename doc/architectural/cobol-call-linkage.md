# Design: whole-program CALL linkage

Status: first increment implemented. Author: Kiro.

Grounded in the IBM Enterprise COBOL for z/OS 6.4 Language Reference,
"CALL statement" and "The PROCEDURE DIVISION header" (USING / RETURNING).

## Goal

Verify a calling program together with the programs it `CALL`s, instead of
abstracting every `CALL` as a havoc of its output arguments. This turns the
front-end from a single-program checker into a (bounded) whole-program one
for callees that are available in the same translation unit.

## Background

Each `PROGRAM-ID` in a source file is lowered to a goto-function
`cobol::<program-id>`; the frontend already parses several programs from one
file. A `CALL "name" USING a b …` passes arguments to the named program,
which receives them through its `PROCEDURE DIVISION USING f1 f2 …` LINKAGE
formals. The default is BY REFERENCE (the callee operates on the caller's
storage); BY CONTENT / BY VALUE pass a copy.

Previously every non-intrinsic `CALL` was a sound stub: BY REFERENCE /
RETURNING receivers were havoced, BY CONTENT / BY VALUE left unchanged.

## Mechanism (copy-in / call / copy-out)

1. **Signatures.** When a program finishes parsing, its signature is recorded
   in `program_sigs` (keyed by upper-cased PROGRAM-ID, persisting across
   programs): the goto-function symbol and, for each `PROCEDURE DIVISION
   USING` formal, the LINKAGE item's record byte-array symbol, offset, size,
   and enclosing record size. (`parse_procedure_division` now captures the
   USING list instead of skipping it.)

2. **Lowering a linked CALL.** When the program-name is a literal and that
   program's signature is known, `parse_call` emits:
   - **copy-in**: for each argument, a byte copy of `min(arg, formal)` bytes
     from the argument's storage into the formal's LINKAGE record;
   - a **CALL_PROGRAM** statement, lowered to a `code_function_callt` of the
     callee's function (which operates on its own LINKAGE record symbols);
   - **copy-out**: for each BY REFERENCE argument, a byte copy of the formal
     back into the argument (BY CONTENT / BY VALUE get no copy-out).
   The callee's WORKING-STORAGE records are static symbols initialised once,
   matching the COBOL default that a non-`INITIAL` program retains its
   working storage between calls.

3. **Fallback.** A dynamic call (program-name in a data item), an external
   program, or a forward reference (callee defined later in the file) is not
   linked and keeps the sound havoc stub.

## Why copy-in / copy-out (and its limits)

BY REFERENCE is defined as the callee using the caller's storage directly.
Copy-in / copy-out produces the same final state **when the arguments do not
overlap each other and the callee does not reach the caller's storage by any
other path**; this holds for the overwhelming majority of COBOL and avoids a
pointer / based-storage model. True aliasing (overlapping arguments, or
`SET ADDRESS OF` based LINKAGE, I18) would require passing addresses; that is
the natural next architectural step and is shared with the pointer model.

The byte copy assumes the argument and formal share a representation, which
holds when their PICTURE / USAGE match — exactly the condition under which
BY REFERENCE is well defined.

## Known limitations (follow-up increments)

- **Forward references** (caller before callee) are not linked: linking needs
  the callee's signature, which is only known after it is parsed. A two-pass
  scheme (collect all signatures, then lower bodies) would remove this. A
  multi-program file currently also needs `--function <main>` because the
  entry-point picker treats several candidate programs as ambiguous.
- **RETURNING** is consumed but the callee's RETURNING item is not yet bound;
  the receiver is havoced.
- **Aliasing** between arguments (or via based LINKAGE) is not modelled.

## Testing

CORE `call-linkage` (a BY REFERENCE update is visible to the caller) and
`call-by-content` (a BY CONTENT update is not). Both select the caller with
`--function`.
