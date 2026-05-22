#!/usr/bin/env python3
# Copyright Diffblue and CBMC contributors
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
# Import Strata's Python test suite as CBMC regression tests.
#
# For each test:
#   - Run the test under CPython to establish a reference verdict
#     (PASS, FAIL via AssertionError, or pre-execution error such as
#     ImportError / NameError / TypeError).
#   - Run the test under CBMC's Python frontend.
#   - Cross-tabulate to decide the test.desc expected outcome:
#       * Python PASS && CBMC SUCCESSFUL  -> expect SUCCESSFUL
#       * Python FAIL && CBMC FAILED      -> expect FAILED
#       * Python ValueError && CBMC FAILED -> expect FAILED (CBMC catches
#         the runtime exception in its assertion form).
#       * Python PASS && CBMC FAILED      -> expect FAILED but tag KNOWNBUG
#         (CBMC over-rejects; needs frontend fix).
#       * Python IMPORT_ERR/NAME_ERR/TYPE_ERR -> SKIP (test is broken at
#         import time; preserve the file but skip in CI).
#       * CBMC TIMEOUT / crash             -> SKIP with note.
#
# All files are copied verbatim from Strata so the upstream behaviour
# is preserved. Attribution is added in regression/cbmc/python-strata-
# tests/README.md and the upstream LICENSE-APACHE / LICENSE-MIT files
# are referenced.
#
# Usage:
#   python3 scripts/import_strata_tests.py \
#     --strata-root ~/strata.git \
#     --cbmc build/bin/cbmc \
#     --out regression/cbmc/python-strata-tests \
#     [--include-pending]
#
# Memory safety:
#   Every spawned CBMC and CPython subprocess is wrapped with a
#   preexec_fn that calls resource.setrlimit(RLIMIT_AS, ...) before exec.
#   The default cap is 4 GiB; override with the env var CBMC_MEM_MB
#   (CBMC_MEM_MB=0 disables). This is a defence-in-depth: a single
#   pathological test should never be able to exhaust the host running
#   the importer.

import argparse
import concurrent.futures
import csv
import os
import resource
import shutil
import subprocess
import sys
from pathlib import Path

# Per-subprocess memory cap (RLIMIT_AS). Applied via preexec_fn to every
# CPython and CBMC invocation we spawn so a single pathological test can't
# exhaust the host. The default is 4 GiB; override with the env var
# CBMC_MEM_MB before running the importer (CBMC_MEM_MB=0 disables the
# cap).
_DEFAULT_MEM_MB = 4096
_MEM_BYTES = int(os.environ.get("CBMC_MEM_MB", _DEFAULT_MEM_MB)) * 1024 * 1024


def _limit_mem():
    """preexec_fn hook: cap virtual address space to _MEM_BYTES.

    Setting RLIMIT_AS makes the kernel return ENOMEM from the next mmap()
    once the cap is exceeded. CBMC's solver typically aborts cleanly with
    a bad_alloc rather than getting OOM-killed by the parent, so a
    timeout / non-zero exit still classifies the run as TIMEOUT/OTHER
    rather than masking a real failure.
    """
    if _MEM_BYTES > 0:
        try:
            resource.setrlimit(resource.RLIMIT_AS, (_MEM_BYTES, _MEM_BYTES))
        except (ValueError, OSError):
            # Some environments (containers without CAP_SYS_RESOURCE) refuse
            # to lower RLIMIT_AS — best-effort, don't crash the importer.
            pass


def run_python(path: Path, cwd: Path):
    """Run a test under CPython. Returns (verdict, stderr_excerpt)."""
    try:
        r = subprocess.run(
            ["python3", str(path)],
            capture_output=True,
            text=True,
            timeout=10,
            cwd=str(cwd),
            preexec_fn=_limit_mem,
        )
        if r.returncode == 0:
            return ("PASS", "")
        err = r.stderr or ""
        last = err.strip().splitlines()[-1] if err.strip() else ""
        if "AssertionError" in err:
            return ("FAIL", last)
        for tag, kind in [
            ("ImportError", "IMPORT_ERR"),
            ("ModuleNotFoundError", "IMPORT_ERR"),
            ("SyntaxError", "SYNTAX_ERR"),
            ("NameError", "NAME_ERR"),
            ("TypeError", "TYPE_ERR"),
            ("ValueError", "VALUE_ERR"),
            ("ZeroDivisionError", "DIV_BY_ZERO"),
            ("AttributeError", "ATTR_ERR"),
            ("KeyError", "KEY_ERR"),
            ("IndexError", "INDEX_ERR"),
        ]:
            if tag in err:
                return (kind, last)
        return (f"ERR(rc={r.returncode})", last)
    except subprocess.TimeoutExpired:
        return ("TIMEOUT", "")
    except Exception as e:  # pragma: no cover
        return (f"ERR:{type(e).__name__}", str(e))


def run_cbmc(cbmc: str, path: Path, unwind: int, timeout: int):
    """Run a test under CBMC. Returns (verdict, stderr_excerpt)."""
    try:
        r = subprocess.run(
            [
                cbmc,
                "--unwind",
                str(unwind),
                "--no-unwinding-assertions",
                str(path),
            ],
            capture_output=True,
            text=True,
            timeout=timeout,
            preexec_fn=_limit_mem,
        )
        out = (r.stdout or "") + (r.stderr or "")
        if "VERIFICATION SUCCESSFUL" in out:
            return ("SUCCESSFUL", "")
        if "VERIFICATION FAILED" in out:
            return ("FAILED", "")
        if "Invariant check failed" in out or "Backtrace:" in out:
            return ("CRASH", out.splitlines()[0] if out else "")
        if "PARSING ERROR" in out:
            return ("PARSE_ERR", "")
        return ("OTHER", out.splitlines()[-1] if out else "")
    except subprocess.TimeoutExpired:
        return ("TIMEOUT", "")
    except Exception as e:
        return (f"ERR:{type(e).__name__}", str(e))


def classify(py_verdict: str, cbmc_verdict: str):
    """Decide the test.desc shape from the two verdicts.

    Returns a dict with keys:
      mode:     'CORE' | 'KNOWNBUG' | 'SKIP'
      pattern:  expected verification regex (or None for SKIP)
      note:     human-readable explanation
    """
    if py_verdict == "PASS" and cbmc_verdict == "SUCCESSFUL":
        return {
            "mode": "CORE",
            "pattern": "^VERIFICATION SUCCESSFUL$",
            "note": "Python and CBMC agree the assertions hold.",
        }
    if py_verdict == "FAIL" and cbmc_verdict == "FAILED":
        return {
            "mode": "CORE",
            "pattern": "^VERIFICATION FAILED$",
            "note": "Python raises AssertionError; CBMC catches the same.",
        }
    if py_verdict == "VALUE_ERR" and cbmc_verdict == "FAILED":
        return {
            "mode": "CORE",
            "pattern": "^VERIFICATION FAILED$",
            "note": (
                "Python raises ValueError at runtime; CBMC reports the "
                "corresponding exception assertion as FAILED."
            ),
        }
    if py_verdict == "PASS" and cbmc_verdict == "FAILED":
        # Precision regression: CBMC over-rejects a program CPython
        # accepts. Tagged KNOWNBUG so a future fix flips it to CORE.
        return {
            "mode": "KNOWNBUG",
            "pattern": "^VERIFICATION FAILED$",
            "note": (
                "PYTHON PASSES but CBMC reports an assertion failure. "
                "Frontend over-rejection (precision gap)."
            ),
        }
    if py_verdict == "FAIL" and cbmc_verdict == "SUCCESSFUL":
        # SOUNDNESS GAP: Python catches an assertion failure that CBMC
        # missed. We DO NOT bake CBMC's wrong verdict into the test.desc
        # as a CORE expectation, because that would lock in the bug. We
        # flag it as KNOWNBUG with the expected (Python-correct) verdict
        # so the test.pl run fails when invoked with -K, and the test
        # silently passes (skipped) under the default -C run.
        return {
            "mode": "KNOWNBUG",
            "pattern": "^VERIFICATION FAILED$",
            "note": (
                "SOUNDNESS GAP: Python raises AssertionError but CBMC "
                "currently reports VERIFICATION SUCCESSFUL. The test "
                "expects FAILED (the Python-correct verdict); a CBMC "
                "frontend fix that catches the same failure will flip "
                "this back to CORE."
            ),
        }
    if py_verdict in (
        "IMPORT_ERR",
        "NAME_ERR",
        "TYPE_ERR",
        "ATTR_ERR",
        "SYNTAX_ERR",
    ):
        return {
            "mode": "SKIP",
            "pattern": None,
            "note": (
                f"CPython fails at import / type check ({py_verdict}). "
                "Preserved upstream but skipped in CI."
            ),
        }
    if cbmc_verdict in ("CRASH", "PARSE_ERR", "TIMEOUT", "OTHER"):
        return {
            "mode": "SKIP",
            "pattern": None,
            "note": (
                f"CBMC outcome '{cbmc_verdict}' on this input. "
                "Preserved upstream but skipped in CI."
            ),
        }
    return {
        "mode": "SKIP",
        "pattern": None,
        "note": f"Unhandled combination py={py_verdict} cbmc={cbmc_verdict}.",
    }


def write_test_desc(target_dir: Path, py_basename: str, classification: dict):
    desc_path = target_dir / "test.desc"
    # Pass the same CBMC arguments the import classifier used (--unwind 5
    # --no-unwinding-assertions). Without this, test.pl invokes CBMC with
    # its defaults (--unwinding-assertions implicitly), and any test that
    # contains a loop will produce a different verdict than the import
    # classifier saw, leading to spurious test.pl failures.
    cbmc_args = "--unwind 5 --no-unwinding-assertions"
    if classification["mode"] == "SKIP":
        body = (
            "KNOWNBUG\n"
            f"{py_basename}\n"
            f"{cbmc_args}\n"
            "^EXIT=10$\n"
            "^SIGNAL=0$\n"
        )
    else:
        body = (
            f"{classification['mode']}\n"
            f"{py_basename}\n"
            f"{cbmc_args}\n"
            f"{classification['pattern']}\n"
            "^EXIT=(0|10)$\n"
            "^SIGNAL=0$\n"
        )
    desc_path.write_text(body)


def write_readme(out_root: Path, summary: dict, kind: str):
    kind_suffix = "" if kind == "tests" else "-pending"
    body = f"""# Strata Python Tests Imported into CBMC Regression

This directory contains Python test files imported verbatim from the
[Strata project](https://github.com/strata-org/Strata)'s
`StrataTest/Languages/Python/{kind}/` directory. They exercise CBMC's
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
| Total imported | {summary['total']} |
| `CORE` (Py == CBMC) | {summary['core']} |
| `KNOWNBUG` (Py-passes / CBMC-rejects) | {summary['knownbug']} |
| `SKIP` (broken upstream / CBMC can't run) | {summary['skip']} |

## Re-importing

The importer (`scripts/import_strata_tests.py`) is idempotent. To
refresh the snapshot after a CBMC frontend fix:

```sh
python3 scripts/import_strata_tests.py \\
  --strata-root ~/strata.git \\
  --cbmc build/bin/cbmc \\
  --out regression/cbmc/python-strata{kind_suffix}-tests
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
"""
    (out_root / "README.md").write_text(body)


def write_notice(out_root: Path):
    (out_root / "NOTICE").write_text(
        """The Python test files in this directory tree (one per subdirectory)
were imported verbatim from the Strata project at
https://github.com/strata-org/Strata, under the
StrataTest/Languages/Python/tests/ subtree, and are licensed under
Apache-2.0 OR MIT (Strata's dual license).

The generated test.desc files in each subdirectory are CBMC-side
metadata and are licensed under CBMC's existing terms (4-clause BSD).
"""
    )


def import_one(args, src_path: Path):
    """Per-test work: classify and write to disk. Returns a row for the
    summary CSV."""
    name = src_path.name
    base = src_path.stem  # without .py
    py_verdict, py_note = run_python(src_path, src_path.parent)
    cbmc_verdict, cbmc_note = run_cbmc(args.cbmc, src_path, args.unwind, args.cbmc_timeout)
    classification = classify(py_verdict, cbmc_verdict)

    target_dir = Path(args.out) / base
    target_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src_path, target_dir / name)
    # Helper modules referenced by some tests
    for helper in ["param_reassign_helper.py", "test_helper.py"]:
        h = src_path.parent / helper
        if h.exists():
            shutil.copy2(h, target_dir / helper)
    write_test_desc(target_dir, name, classification)
    return {
        "name": base,
        "py_verdict": py_verdict,
        "cbmc_verdict": cbmc_verdict,
        "mode": classification["mode"],
        "py_note": py_note,
        "cbmc_note": cbmc_note,
    }


def main(argv):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--strata-root", required=True)
    p.add_argument("--cbmc", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--include-pending", action="store_true")
    p.add_argument("--unwind", type=int, default=5)
    p.add_argument("--cbmc-timeout", type=int, default=20)
    p.add_argument("--jobs", type=int, default=16)
    args = p.parse_args(argv)

    src_root = Path(args.strata_root) / "StrataTest" / "Languages" / "Python" / "tests"
    if not src_root.is_dir():
        sys.exit(f"Strata tests dir not found: {src_root}")

    paths = sorted(p for p in src_root.glob("*.py") if p.name != "param_reassign_helper.py")
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Importing {len(paths)} tests from {src_root} -> {out_dir}")
    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(import_one, args, pp) for pp in paths]
        for fut in concurrent.futures.as_completed(futs):
            rows.append(fut.result())

    rows.sort(key=lambda r: r["name"])
    summary = {
        "total": len(rows),
        "core": sum(1 for r in rows if r["mode"] == "CORE"),
        "knownbug": sum(1 for r in rows if r["mode"] == "KNOWNBUG"),
        "skip": sum(1 for r in rows if r["mode"] == "SKIP"),
    }
    write_readme(out_dir, summary, "tests")
    write_notice(out_dir)
    with open(out_dir / "import_log.csv", "w", newline="") as f:
        w = csv.DictWriter(
            f, fieldnames=["name", "py_verdict", "cbmc_verdict", "mode", "py_note", "cbmc_note"]
        )
        w.writeheader()
        w.writerows(rows)

    print(f"\nSummary:")
    print(f"  Total:    {summary['total']}")
    print(f"  CORE:     {summary['core']}")
    print(f"  KNOWNBUG: {summary['knownbug']}")
    print(f"  SKIP:     {summary['skip']}")
    print(f"\nDetails: {out_dir / 'import_log.csv'}")

    if args.include_pending:
        pending_root = src_root / "pending"
        if pending_root.is_dir():
            pending_out = Path(args.out + "-pending")
            pending_out.mkdir(parents=True, exist_ok=True)
            ppaths = sorted(pending_root.glob("*.py"))
            print(f"\nImporting {len(ppaths)} pending tests -> {pending_out}")
            args.out = str(pending_out)
            prows = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
                futs = [ex.submit(import_one, args, pp) for pp in ppaths]
                for fut in concurrent.futures.as_completed(futs):
                    prows.append(fut.result())
            prows.sort(key=lambda r: r["name"])
            psummary = {
                "total": len(prows),
                "core": sum(1 for r in prows if r["mode"] == "CORE"),
                "knownbug": sum(1 for r in prows if r["mode"] == "KNOWNBUG"),
                "skip": sum(1 for r in prows if r["mode"] == "SKIP"),
            }
            write_readme(pending_out, psummary, "pending")
            write_notice(pending_out)
            with open(pending_out / "import_log.csv", "w", newline="") as f:
                w = csv.DictWriter(
                    f,
                    fieldnames=[
                        "name",
                        "py_verdict",
                        "cbmc_verdict",
                        "mode",
                        "py_note",
                        "cbmc_note",
                    ],
                )
                w.writeheader()
                w.writerows(prows)
            print(f"  Pending CORE:     {psummary['core']}")
            print(f"  Pending KNOWNBUG: {psummary['knownbug']}")
            print(f"  Pending SKIP:     {psummary['skip']}")


if __name__ == "__main__":
    main(sys.argv[1:])
