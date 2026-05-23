# Strata Python Tests Imported into CBMC Regression

This directory contains Python test files imported verbatim from the
[Strata project](https://github.com/strata-org/Strata)'s
`StrataTest/Languages/Python/tests/` directory. They exercise CBMC's
Python frontend with a third-party test corpus.

Each subdirectory is a single CBMC regression test: the `.py` source
file is copied unmodified from upstream, and a generated `test.desc`
encodes the expected verification outcome.

## How `test.desc` is decided

For every test the importer ran the source through:

  1. **CPython** — to determine the Python-language ground truth
     (`PASS` if all `assert` statements hold, `FAIL` if any raises
     `AssertionError`, or one of `ImportError`/`NameError`/...).
  2. **CBMC's Python frontend** — to record the current CBMC verdict.

The cross-tabulation drives the `test.desc` shape:

| Python                  | CBMC          | `test.desc`                    |
|-------------------------|---------------|--------------------------------|
| `PASS`                  | `SUCCESSFUL`  | `CORE`, expect `^VERIFICATION SUCCESSFUL$` |
| `FAIL`                  | `FAILED`      | `CORE`, expect `^VERIFICATION FAILED$`     |
| `VALUE_ERR` (e.g. shift-by-negative) | `FAILED` | `CORE`, expect `^VERIFICATION FAILED$` (CBMC catches the runtime exception) |
| `PASS`                  | `FAILED`      | `KNOWNBUG`, expect `^VERIFICATION FAILED$` (CBMC over-rejects, future fix flips it to CORE) |
| `IMPORT_ERR` / `NAME_ERR` / `TYPE_ERR` | (any) | `KNOWNBUG`, skipped (test is broken at import time, preserved for traceability) |
| CBMC `CRASH`/`PARSE_ERR`/`TIMEOUT` | (any) | `KNOWNBUG`, skipped |

## Importer numbers (this snapshot)

| Bucket | Count |
|---|---:|
| Total imported | 219 |
| `CORE` (Py == CBMC) | 174 |
| `KNOWNBUG` (Py-passes / CBMC-rejects) | 19 |
| `SKIP` (broken upstream / CBMC can't run) | 26 |

## Re-importing

The importer (`scripts/import_strata_tests.py`) is idempotent. To
refresh the snapshot after a CBMC frontend fix:

```sh
python3 scripts/import_strata_tests.py \
  --strata-root ~/strata.git \
  --cbmc build/bin/cbmc \
  --out regression/cbmc/python-strata-tests
```

The script will re-run CPython and CBMC for every test and rewrite
`test.desc` files in place. Existing `.py` files are not modified.

## Attribution

Sources are licensed under Apache-2.0 OR MIT (see Strata's
`LICENSE-APACHE` and `LICENSE-MIT`). The unmodified upstream tree is
preserved on disk so any local ESBMC-Python-style amendments live
alongside the originals; if a test required substantive amending to
make CBMC's frontend accept it, the amendment is documented in the
test's `test.desc` comment and the rationale notes here.
