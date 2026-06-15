# Design: implicit runtime-property checks

Status: design + initial implementation. Author: Kiro.

This document defines the **implicit safety properties** the COBOL
frontend checks — the COBOL analogue of CBMC's built-in C checks
(`--bounds-check`, `--div-by-zero-check`, …) and of "absence of uncaught
exceptions" in Java/Python. Until now the frontend verified only
user-written assertions (`CALL "__CPROVER_assert"`); these checks make it
prove COBOL's own runtime-fault conditions.

Grounded in the IBM Enterprise COBOL for z/OS 6.4 Language Reference
(*LR*) and the z/OS runtime behaviour the corresponding conditions raise.

## Why the C checks do not suffice

The frontend lowers a subscripted/`reference-modified` access to a
`byte_extract`/`byte_update` at a computed offset into the record
byte-array, and division to a value-domain `div_exprt`. CBMC's
`--bounds-check` instruments `index_exprt`/dereferences (not `byte_*`
offset overruns) and `--div-by-zero-check` did not fire on the
value-domain division (both confirmed by experiment: an `OCCURS 3`
indexed at 5 and a divide-by-zero both reported VERIFICATION SUCCESSFUL).
So the checks must be emitted by the frontend, where the COBOL-level
quantities (subscript value, occurrence count, divisor, field size) are
known.

## Properties (property classes)

| class | condition checked | LR basis |
|---|---|---|
| `cobol:subscript-range` | each subscript `s` satisfies `1 <= s <= occurs` | "Subscripting"; SSRANGE |
| `cobol:refmod-range` | `start >= 1` and `start + length - 1 <= size` | "Reference modification"; SSRANGE |
| `cobol:division-by-zero` | the divisor is non-zero | "DIVIDE"/"COMPUTE"; the SIZE ERROR condition |

(Future: `cobol:uninitialized` read-before-write, and `cobol:numeric`
data exception — S0C7 — once faithful numeric content is broad.)

## The "pending checks" mechanism (architectural)

A check is a *statement* (an `ASSERT`), but subscripts / reference
modification / divisors are parsed deep in *expression* context
(`parse_ref`, `divide_values`), which yields `exprt`/`reft`, not
statements. Rather than thread statement output through every expression
helper, the typechecker carries a small **`pending_checks` buffer**: an
expression helper appends an `ASSERT` (condition + property class) to it,
and `parse_statement` drains the checks accumulated for the current
statement and emits them **immediately before** that statement.

Draining is stack-disciplined so compound statements nest correctly:
`parse_statement` records the buffer size on entry, dispatches (which may
add checks for the statement's own operands and recursively parse nested
statements — each draining its own range), then emits exactly the range
it added. So an `IF` emits its condition's subscript checks before the
`IF`, while a nested statement's checks sit inside the branch. This is
the general pattern: *checks arising in an earlier (expression)
representation are buffered and materialised at the next statement
boundary* — the same "carry forward what a later phase needs" principle
used elsewhere (e.g. token adjacency for COPY REPLACING).

The subscript value is evaluated from the same variables the statement
reads, and nothing between the check and the statement changes them, so
asserting just before the statement matches the COBOL evaluation point.

## On/off switch

The checks are **gated on CBMC's `bounds-check` option** (subscript and
reference-modification range *are* bounds checks). Since CBMC v6 enables the
standard checks by default, the COBOL checks are **on by default** and are
turned off together with the C checks by `--no-standard-checks`. They are
emitted as normal `ASSERT`s with the property classes above, so they are also
selectable through CBMC's `--property` machinery. The gate is wired in
`cobol_languaget::set_language_options` (reads the `bounds-check` option) and
threaded into the typechecker.

Because real programs index tables / reference-modify with values that are not
always provably in range, the checks surface genuine *potential* overruns. On
the AWS CardDemo corpus (`--unwind 3`) they flag four, all genuine: a table
indexed by a page number from the CICS commarea with no upper-bound guard
(COPAUS0C), and three `X(1:len)` reference modifications whose `len` is an
unvalidated external length — a message length (COPAUA0C), a LINKAGE length
(CBSTM03B), and a **DB2 VARCHAR length field** used directly as the refmod
length (COTRTUPC). The last is a classic, serious mainframe defect class.
Running with `--no-standard-checks` reproduces the prior clean user-assertion
baseline (44/44, zero VERIFICATION FAILED).

### Loop-condition placement

A subscript/refmod is parsed in expression context but a check is a statement,
so checks are buffered (`pending_checks`) and emitted at the next statement
boundary. For a loop whose **condition** references the loop variable (e.g.
`PERFORM VARYING i ... UNTIL S(i:1) = SPACE`, or a `SEARCH` WHEN that
subscripts by the index), emitting the check before the statement would test
the variable's pre-loop (nondet) value and raise a false alarm. So the
condition's checks are moved into the loop body (`stmtt::cond_checks`) and
re-emitted before each evaluation of the condition, matching the
per-iteration value. This is implemented for `PERFORM UNTIL`,
`PERFORM VARYING`, and `SEARCH`.


## Testing

Each check has positive regression tests (in-range / non-zero → SUCCESS)
and negative ones (out-of-range / zero → the named property FAILS), plus
differential cross-checks: a program that abends under GnuCOBOL/z/OS
(e.g. a subscript overrun) is one our checker should flag.
