\file
# Python stdlib parsing integration test

This integration test exercises the CBMC Python front-end against the
real CPython standard library source tree. Its purpose is to make sure
that progress on broadening the set of stdlib modules we can ingest
(Step 1 of `doc/architectural/python-module-support-plan.md`) is
monitored by CI and cannot silently regress.

## What the test does

`run_stdlib_parse_test.sh` runs `cbmc` against each of a curated set of
stdlib modules (see `modules.txt`) with a short per-module wall-clock
budget, and records for each module:

* process exit code,
* whether the run crashed (invariant violation / abort),
* whether the run hit the wall-clock budget,
* the first reported internal error location (if any).

The results are compared against `baseline.csv`. The test fails if any
module regresses (e.g. starts crashing, starts timing out, or begins
producing an internal error it was not producing before). The test does
**not** fail when a module starts passing cleanly — improvements are
always welcome.

## What the test does NOT do

It does **not** attempt to verify the modules. Many stdlib modules
contain unverifiable code (use of unmodelled built-ins, unannotated
function parameters, dynamic features that we cannot yet represent).
For the purposes of this test, "passes" means "ingested without the
front-end itself crashing or hanging".

## Running locally

```sh
CBMC=$PWD/build/bin/cbmc ./run_stdlib_parse_test.sh
```

The default `CBMC` is `$HOME/cbmc-python.git/build/bin/cbmc`; set the
environment variable when building elsewhere. `STDLIB` defaults to
`/usr/lib/python3.12` and may be overridden to test against a different
Python installation.

To refresh the baseline after an intentional improvement:

```sh
CBMC=$PWD/build/bin/cbmc ./run_stdlib_parse_test.sh --update-baseline
```

Review the diff carefully before committing.
