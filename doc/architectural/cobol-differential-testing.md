# Differential validation against GnuCOBOL

Status: implemented (seed harness). Author: Kiro.

This is a differential-testing harness that cross-checks the CBMC COBOL
frontend's *semantics* against an independent COBOL implementation
(GnuCOBOL / `cobc`). It exists because the frontend encodes a large set
of IBM Language Reference semantic decisions (zoned/packed encoding,
de-editing, edited-MOVE formatting, zoned comparison, INSPECT/STRING byte
semantics, arithmetic scaling, ...) that are unit-tested individually but
whose *unknown* divergences can only be found by comparing to a real
compiler. It complements the catalogue in
`cobol-frontend-architecture.md` §5 (which lists the *known* gaps): this
harness hunts for the unknown ones.

## Method

Each program in `regression/cobol-diff/programs/*.cob` is **deterministic**
(no external input), computes a result into a field, and prints it on one
line as `[<field>]`. For each program the driver
(`regression/cobol-diff/diff_check.py`):

1. compiles and runs it with `cobc -x` to obtain the **oracle** — the
   exact bytes GnuCOBOL produces for the result field (captured between
   the `[` and `]` markers, so trailing spaces are preserved);
2. builds a CBMC variant in which the `DISPLAY "[" <field> "]"` line is
   replaced by `CALL "__CPROVER_assert" USING <field> = '<oracle>'`;
3. runs that variant through CBMC.

`VERIFICATION SUCCESSFUL` means the frontend's modelled result equals
GnuCOBOL's actual output for that program — strong evidence the modelling
is faithful, because the two are independent implementations. A
`VERIFICATION FAILED` is a real divergence (a frontend soundness or
precision bug). CBMC runs are memory- and time-bounded in the driver.

Because the comparison is on the **bytes** of the result field, the seed
programs favour alphanumeric / numeric-edited results, which is exactly
where the recent byte-level semantics (STRING, INSPECT, de-editing,
edited MOVE, group-move padding, reference modification) live.

## Running

```sh
cd regression/cobol-diff
./diff_check.py --cbmc ../../build/bin/cbmc
```

Requires `cobc` (GnuCOBOL) on `PATH`. Exit status is non-zero if any
program diverges or errors. This is intended to be run alongside the
regression suite as a semantic safety net; it is *not* part of the
`test.pl` CORE run (it needs GnuCOBOL).

## Findings

- **DIVIDE precision (fixed).** The first run found that `DIVIDE 1000 BY 3
  GIVING x ROUNDED` produced `333.00` instead of `333.33`: the quotient
  was computed by integer division at scale 0, discarding the fractional
  digits before the receiver's scale was applied. Fixed by developing the
  quotient at a guard scale (IBM LR "DIVIDE statement": the quotient is
  computed to the receivers' decimal places, plus a guard for ROUNDED);
  REMAINDER still uses the truncated integer quotient. Regression test
  `regression/cobol/divide-precision`.

## Dialect notes / caveats

- GnuCOBOL is not IBM Enterprise COBOL; the two agree on the core ANSI
  semantics exercised here, but a *deliberate* IBM-vs-GnuCOBOL difference
  (e.g. EBCDIC collating, `HIGH-VALUE`, some intrinsics) would show as a
  spurious "divergence". Keep seed programs within the common subset, or
  annotate known dialect differences when they arise.
- The harness checks the result *value*, not control-flow or
  side effects; DISPLAY is the observation channel.

## Extending

Add a `programs/d-*.cob` that computes a result into a field and ends with
`DISPLAY "[" <field> "]".  STOP RUN.`. Favour constructs whose byte-level
result is the point of the test. Candidates to add as the character-
semantics work continues: UNSTRING, INSPECT REPLACING, STRING with a
delimiter, signed numeric editing, and de-editing edge cases.
