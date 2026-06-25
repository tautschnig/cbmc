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
import resource
import subprocess
import sys
import time
from pathlib import Path

# Per-CBMC subprocess memory cap (RLIMIT_AS). Applied via preexec_fn so a
# pathological test on the sweep can't take down the host. Override with
# CBMC_MEM_MB before running; CBMC_MEM_MB=0 disables.
_MEM_BYTES = int(os.environ.get("CBMC_MEM_MB", "4096")) * 1024 * 1024


def _limit_mem():
    if _MEM_BYTES > 0:
        try:
            resource.setrlimit(resource.RLIMIT_AS, (_MEM_BYTES, _MEM_BYTES))
        except (ValueError, OSError):
            pass


def parse_desc(path: Path):
    """Return (tag, source, expected_lines).

    Test descriptors look like:
      line 0: TAGS
      line 1: source
      line 2: command-line args (may be empty / absent)
      line 3+: zero or more blank lines then expected regex(es)

    Some descriptors omit the args line entirely (e.g.
    github_2879_5/test.desc). Heuristic: if line 2 starts with
    a regex anchor ('^') we treat it as the start of the regex
    block; otherwise it's args and the regex block begins at
    line 3 (skipping blanks)."""
    text = path.read_text(errors="replace").splitlines()
    tag = text[0].strip() if len(text) > 0 else ""
    source = text[1].strip() if len(text) > 1 else ""
    # Determine where the regex block starts.
    regex_start = 3
    if (
        len(text) > 2
        and text[2].strip().startswith("^")
        and not text[2].strip().startswith("--")
    ):
        regex_start = 2
    expected = []
    saw_any = False
    for line in text[regex_start:]:
        if line.strip() == "":
            if saw_any:
                break
            continue  # Skip blank lines BEFORE the regex section
        expected.append(line)
        saw_any = True
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


def run_one(test_dir: Path, cbmc: str, timeout_s: int, unwind: int,
            extra_cbmc_flags=None, triage_bound=False):
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

    # Extract CBMC-compatible flags from the args line (line 2).
    # Most ESBMC flags aren't understood by CBMC, so we only pass
    # through a safe-list of options that CBMC supports natively.
    extra_flags: list[str] = []
    text = desc_file.read_text(errors="replace").splitlines()
    if len(text) > 2 and text[2].strip().startswith("--"):
        SAFE_FLAGS_TAKES_VAL = {
            "--function",
            "--unwind",
            "--object-bits",
        }
        SAFE_FLAGS_NO_VAL = {
            "--overflow-check",
            "--signed-overflow-check",
            "--unsigned-overflow-check",
            "--pointer-overflow-check",
            "--float-overflow-check",
            "--no-pointer-check",
            "--no-bounds-check",
            "--no-div-by-zero-check",
            "--no-standard-checks",
            "--no-signed-overflow-check",
            "--no-unwinding-assertions",
            "--unwinding-assertions",
        }
        toks = text[2].split()
        i = 0
        while i < len(toks):
            t = toks[i]
            # ESBMC flag aliases — translate to our equivalents.
            if t == "--strict-types":
                # ESBMC's --strict-types corresponds to our
                # --python-check-annotations: emit a property
                # whenever an AnnAssign or call-site argument's
                # type doesn't match the declared annotation.
                extra_flags.append("--python-check-annotations")
                i += 1
                continue
            if t == "--is-instance-check":
                # ESBMC's --is-instance-check is structurally
                # the same as --strict-types: it asks the
                # frontend to emit a property whenever a value's
                # runtime shape doesn't match the declared
                # type annotation. Maps to our
                # --python-check-annotations.
                extra_flags.append("--python-check-annotations")
                i += 1
                continue
            if t == "--incremental-bmc":
                # ESBMC's incremental-BMC mode catches
                # functions that fall off the end without
                # returning a value when an annotated return
                # type was promised. Our equivalent is the
                # --python-missing-return-check flag.
                extra_flags.append("--python-missing-return-check")
                i += 1
                continue
            if t in SAFE_FLAGS_TAKES_VAL and i + 1 < len(toks):
                extra_flags.extend([t, toks[i + 1]])
                i += 2
            elif t in SAFE_FLAGS_NO_VAL:
                extra_flags.append(t)
                i += 1
            else:
                i += 1

    # Default --unwind only when the test didn't override it.
    cmd = [cbmc]
    if "--unwind" not in extra_flags:
        cmd += ["--unwind", str(unwind)]
    if "--no-unwinding-assertions" not in extra_flags and \
       "--unwinding-assertions" not in extra_flags:
        cmd.append("--no-unwinding-assertions")
    cmd.extend(extra_flags)
    if extra_cbmc_flags:
        cmd.extend(extra_cbmc_flags)
    cmd.append(str(main_py))
    start = time.monotonic()
    try:
        cp = subprocess.run(
            cmd,
            cwd=str(test_dir),
            capture_output=True,
            text=True,
            timeout=timeout_s,
            errors="replace",
            preexec_fn=_limit_mem,
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
        # Bound-artifact triage (P2): a "CBMC=SUCCESSFUL but expected=FAILED"
        # DIFF may be a genuine false proof OR just the bug being deeper than
        # the unwind bound (the default --no-unwinding-assertions silently
        # assumes loops terminate within the bound). Re-run with
        # --unwinding-assertions: if an unwinding assertion fails, the
        # SUCCESSFUL was bound-limited (outcome BOUND, sound w.r.t. the bound);
        # if it still verifies, the bound is adequate -> a genuine candidate
        # false proof worth investigating.
        if (
            triage_bound
            and outcome == "DIFF"
            and verdict == "SUCCESSFUL"
            and expected
            and "FAILED" in expected[0]
        ):
            tcmd = [c for c in cmd if c != "--no-unwinding-assertions"]
            if "--unwinding-assertions" not in tcmd:
                tcmd.insert(1, "--unwinding-assertions")
            try:
                tcp = subprocess.run(
                    tcmd,
                    cwd=str(test_dir),
                    capture_output=True,
                    text=True,
                    timeout=timeout_s,
                    errors="replace",
                    preexec_fn=_limit_mem,
                )
                tout = (tcp.stdout or "") + "\n" + (tcp.stderr or "")
                if re.search(
                    r"^VERIFICATION FAILED$", tout, re.M
                ) and re.search(r"unwinding assertion.*FAILURE", tout):
                    outcome = "BOUND"
                    detail = "bound-limited (unwinding assertion fails); " + detail
                elif re.search(r"^VERIFICATION SUCCESSFUL$", tout, re.M):
                    detail = "candidate-false-proof (unwind-adequate); " + detail
            except Exception:
                pass
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
    ap.add_argument("--extra-cbmc-flags", default="",
                    help="extra flags appended to every CBMC invocation "
                         "(space-separated), e.g. '--no-python-ref-mutables'")
    ap.add_argument("--triage-bound", action="store_true",
                    help="for each 'CBMC=SUCCESSFUL but expected=FAILED' DIFF, "
                         "re-run with --unwinding-assertions to classify it as "
                         "BOUND (bug deeper than --unwind, sound) vs a genuine "
                         "candidate false proof")
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
        futures = {pool.submit(run_one, t, args.cbmc, args.timeout, args.unwind,
                               args.extra_cbmc_flags.split(), args.triage_bound): t
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
