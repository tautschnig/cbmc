# CBMC limitations encountered during Linux kernel analysis

A running list of points where CBMC's current capability constrains
the analysis work in `integration/linux/`.  Each entry records:

- a concise statement of the limitation;
- the specific symptom we saw;
- how we worked around it (if we could), and what we deferred (if we
  couldn't);
- any upstream issue/PR reference, once filed.

The file is append-only from this point forward; mark resolved items
inline rather than removing them.

## LIM-001 — `library_check.sh` fails mid-build

**First hit:** M3 rebuild, CBMC commit range `6b6c8d326d..166a7d4af3`.

Building any target that depends on `libansi-c.a` produces

```
[N/M] Generating library-check.stamp
FAILED: src/ansi-c/library-check.stamp ...
Tests and library functions don't match.
```

The diff listed many `*-01` test directory names
(`toupper-01`, `time-01`, `vasprintf-01`, …) under `regression/cbmc-library/`
as "tests not matching any library function."  These names appear in
the working copy of the tree but not in the library function list
that the script computes from `src/*/library/*.c`.  Cause seems to
be a drift between a recent batch of `-01` tests added to
`regression/cbmc-library/` and the library-function catalog, not
anything intrinsic to the tooling.

**Workaround.** Temporarily patch `src/ansi-c/library_check.sh` to
suffix the final `diff -u` with `; true`, rebuild, revert the patch.
No local source change is committed.  See the `M3 rebuild` note in
git history for the exact steps.

**Status.** Works around cleanly.  A more robust fix upstream would
make the check advisory rather than fatal when the mismatch is
`-N` (missing function) only, or factor the catalog generation out of
the regression checker.

## LIM-002 — `goto-instrument --enforce-contract` on a function with a loop requires loop contracts

**First hit:** M2, `page_provenance/set_page_prov`.

Running

```
goto-instrument --enforce-contract set_page_prov <in.gb> <out.gb>
```

on a function whose body contains a `for` loop produces

```
File: src/goto-instrument/contracts/contracts.cpp:1152
Condition: is_loop_free(function_body, ns, log)
Reason: Loops remain in function 'set_page_prov', assigns clause
        checking instrumentation cannot be applied.
```

and aborts with a core dump rather than a diagnostic exit.

Loop contracts (`__CPROVER_loop_invariant`, `__CPROVER_loop_assigns`,
`goto-instrument --apply-loop-contracts`) exist, but the cprover
regression tree carries `quicksort_contracts_01` as `KNOWNBUG` with
the note "Loop invariants are overzealous in deciding what counts as
side effects," suggesting the feature is not robust for loops that
read shared memory.

**Workaround.** Do not `--enforce-contract` functions with loops.
Retain the contracts on them for documentation and for use at call
sites via `--replace-call-with-contract`, and verify functional
invariants through plain-cbmc assertion-style unit tests (see
`page_provenance/test_unit.c`).

**Status.** Partial workaround.  Closing the gap needs either
- robust loop-contract enforcement for loops reading shared memory
  (upstream CBMC work), or
- a redesign of the backing-store so the mutator is loop-free
  (module-local).

Filed as future work in `DESIGN.md` §8 (kernel-driven CBMC C
front-end work track) and the `page_provenance/README.md` "Known
gap" section.

## LIM-003 — `goto-instrument` aborts rather than failing gracefully

**First hit:** concurrent with LIM-002.

When `goto-instrument --enforce-contract` detects a situation it
cannot handle, it aborts (SIGABRT with a core dump) and writes a
Backtrace: dump to stderr.  A user-visible error message with a
non-zero exit code would be easier for scripting and CI, and does not
indicate an internal invariant violation.

**Workaround.** Scripts must be robust against `goto-instrument` not
producing the output file; check for the file's existence and handle
the error path.  `run.sh` in `page_provenance/` catches this.

**Status.** Ergonomic issue rather than a soundness bug; candidate
for a small CBMC contribution.
