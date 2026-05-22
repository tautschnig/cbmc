#!/usr/bin/env python3
"""Run CBMC on ESBMC's Python regression suite and tabulate outcomes.

Invocation strategy:
- Each test is a directory under regression/python/ with main.py and test.desc.
- We IGNORE ESBMC-specific flags from line 3 of test.desc (most are not understood
  by CBMC). Run CBMC with default checks + --unwind <N> to bound loops similar to
  ESBMC's --incremental-bmc behaviour at small unwinds.
- Compare CBMC stdout against the regex(es) on lines 4+ of test.desc.

Categorisation:
- PASS    — first expected regex matched (CBMC and ESBMC agree)
- FAIL    — CBMC ran but expected regex did not match (and CBMC printed
            VERIFICATION SUCCESSFUL/FAILED)
- DIFF    — CBMC and ESBMC disagree on the verdict (one says SUCCESSFUL, the
            other FAILED) — subset of FAIL where we know CBMC's verdict
- TOERR   — CBMC produced no verification verdict (parse error, frontend
            error, etc.)
- TIMEOUT — CBMC killed by timeout
- CRASH   — CBMC died with non-zero / signal exit and no verdict

Outputs CSV: name,tag,expected_regex,outcome,wall_ms,cbmc_verdict,detail
"""
import argparse
import concurrent.futures as cf
import csv
import os
import re
import subprocess
import sys
import time
from pathlib import Path


def parse_desc(path: Path):
    """Return (tag, source, expected_lines) where expected_lines is a list of
    regex strings from line 4 onwards (until the first blank line)."""
    text = path.read_text(errors="replace").splitlines()
    tag = text[0].strip() if len(text) > 0 else ""
    source = text[1].strip() if len(text) > 1 else ""
    expected = []
    for line in text[3:]:
        if line.strip() == "":
            break
        expected.append(line)
    return tag, source, expected


def classify(stdout: str, expected: list[str]):
    """Return (outcome, verdict, matched_regex_or_detail)."""
    # Find CBMC verdict.
    verdict = None
    if re.search(r"^VERIFICATION SUCCESSFUL$", stdout, re.M):
        verdict = "SUCCESSFUL"
    elif re.search(r"^VERIFICATION FAILED$", stdout, re.M):
        verdict = "FAILED"

    if not expected:
        # No expected regex — just record verdict.
        if verdict:
            return ("UNKNOWN", verdict, "no expected regex")
        return ("TOERR", "", "no expected regex, no verdict")

    # Match the first expected regex against stdout (multiline).
    primary = expected[0]
    try:
        if re.search(primary, stdout, re.M):
            return ("PASS", verdict or "", primary)
    except re.error as e:
        return ("TOERR", verdict or "", f"bad regex: {e}")

    # Determine if it's a clean disagreement.
    if verdict and primary in (r"^VERIFICATION SUCCESSFUL$", r"^VERIFICATION FAILED$"):
        return ("DIFF", verdict, f"expected={primary}")

    if verdict is None:
        return ("TOERR", "", f"expected={primary}")

    return ("FAIL", verdict, f"expected={primary}")


def run_one(test_dir: Path, cbmc: str, timeout_s: int, unwind: int):
    desc_file = test_dir / "test.desc"
    main_py = test_dir / "main.py"
    name = test_dir.name
    if not desc_file.is_file() or not main_py.is_file():
        return {
            "name": name,
            "tag": "",
            "expected": "",
            "outcome": "SKIP",
            "wall_ms": 0,
            "verdict": "",
            "detail": "missing test.desc or main.py",
        }

    tag, source, expected = parse_desc(desc_file)
    expected_str = expected[0] if expected else ""

    cmd = [cbmc, "--unwind", str(unwind), "--no-unwinding-assertions", str(main_py)]
    start = time.monotonic()
    try:
        cp = subprocess.run(
            cmd,
            cwd=str(test_dir),
            capture_output=True,
            text=True,
            timeout=timeout_s,
            errors="replace",
        )
        wall_ms = int((time.monotonic() - start) * 1000)
        out = (cp.stdout or "") + "\n" + (cp.stderr or "")
        outcome, verdict, detail = classify(out, expected)
        if outcome == "TOERR" and cp.returncode and cp.returncode > 1:
            # CBMC-specific exit codes >1 indicate a frontend or solver error.
            outcome = "TOERR"
            detail = f"exit={cp.returncode}; {detail}"
        if cp.returncode < 0:
            outcome = "CRASH"
            detail = f"signal={-cp.returncode}"
    except subprocess.TimeoutExpired:
        wall_ms = timeout_s * 1000
        outcome = "TIMEOUT"
        verdict = ""
        detail = f"killed after {timeout_s}s"
    except Exception as e:
        wall_ms = int((time.monotonic() - start) * 1000)
        outcome = "ERROR"
        verdict = ""
        detail = f"runner exception: {e}"

    return {
        "name": name,
        "tag": tag,
        "expected": expected_str,
        "outcome": outcome,
        "wall_ms": wall_ms,
        "verdict": verdict,
        "detail": detail,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cbmc", default=str(Path.home() / "cbmc-python.git/build/bin/cbmc"))
    ap.add_argument("--regression",
                    default=str(Path.home() / "esbmc.git/regression/python"))
    ap.add_argument("--timeout", type=int, default=30, help="per-test timeout (s)")
    ap.add_argument("--unwind", type=int, default=5)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--out", default="/tmp/cbmc-on-esbmc-python.csv")
    ap.add_argument("--tag", choices=["all", "CORE", "THOROUGH", "KNOWNBUG"],
                    default="CORE",
                    help="only run tests with this tag (or all)")
    ap.add_argument("--limit", type=int, default=0,
                    help="limit number of tests (0 = no limit)")
    ap.add_argument("--filter", default="",
                    help="only run dir names matching this substring")
    args = ap.parse_args()

    reg = Path(args.regression)
    if not reg.is_dir():
        print(f"regression dir not found: {reg}", file=sys.stderr)
        return 2

    tests = sorted([d for d in reg.iterdir() if d.is_dir()])
    if args.filter:
        tests = [t for t in tests if args.filter in t.name]
    if args.tag != "all":
        # Check tag is line 1 of test.desc.
        filtered = []
        for t in tests:
            desc = t / "test.desc"
            if desc.is_file():
                first = desc.read_text(errors="replace").splitlines()
                if first and first[0].strip() == args.tag:
                    filtered.append(t)
        tests = filtered
    if args.limit:
        tests = tests[: args.limit]

    print(f"Running {len(tests)} tests with {args.jobs} workers, "
          f"timeout={args.timeout}s, unwind={args.unwind}")
    print(f"CBMC: {args.cbmc}")
    print(f"Out:  {args.out}\n")

    rows = []
    counts = {}
    completed = 0
    t_start = time.monotonic()
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_one, t, args.cbmc, args.timeout, args.unwind): t
                   for t in tests}
        for fut in cf.as_completed(futures):
            row = fut.result()
            rows.append(row)
            counts[row["outcome"]] = counts.get(row["outcome"], 0) + 1
            completed += 1
            if completed % 100 == 0:
                elapsed = time.monotonic() - t_start
                rate = completed / elapsed if elapsed > 0 else 0
                eta = (len(tests) - completed) / rate if rate > 0 else 0
                print(f"  [{completed}/{len(tests)}] "
                      f"{elapsed:.0f}s  rate={rate:.1f}/s  ETA={eta:.0f}s "
                      f"{counts}")

    rows.sort(key=lambda r: r["name"])
    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(
            f,
            fieldnames=["name", "tag", "expected", "outcome", "wall_ms", "verdict", "detail"],
        )
        w.writeheader()
        for r in rows:
            w.writerow(r)

    elapsed = time.monotonic() - t_start
    print(f"\nDone. {len(rows)} tests in {elapsed:.0f}s.")
    print("Outcomes:")
    for outcome, n in sorted(counts.items(), key=lambda x: -x[1]):
        print(f"  {outcome:8s} {n:5d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
