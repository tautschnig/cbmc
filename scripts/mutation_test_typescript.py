#!/usr/bin/env python3
"""
Mutation tester for the CBMC TypeScript regression test suite.

Usage:
  scripts/mutation_test_typescript.py [--tests DIR1 DIR2 ...]
                                      [--timeout N] [--cbmc PATH]

For each CORE test, applies a set of source-code mutations via sed-
style substitutions and requires the mutated program to FAIL
verification. If any mutation is NOT caught, the test is reported
as potentially tautological (the assertion may pass regardless of
the mutation's effect).

The mutations are deliberately generic — they don't need to match
every test. A test that has no matching mutation is skipped
(but listed in the summary).

Why this matters:
- A CORE test that silently tolerates mutations isn't a regression
  guard; it's a placeholder.
- Mutations catch tautologies like `assert(x === x)` or
  `assert(1 === 1)`.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CBMC = REPO_ROOT / "build" / "bin" / "cbmc"
DEFAULT_REGRESSION = REPO_ROOT / "regression" / "typescript"

# Generic mutations to apply to each test's main.ts. Each is a
# (pattern, replacement, description) tuple. The script applies ONE
# mutation at a time and checks that the test fails. If at least one
# mutation causes the test to fail, the test is "guarded". If no
# mutation applies, the test is "inapplicable". If all applicable
# mutations pass (test still verifies), the test is "tautological".
MUTATIONS = [
    # Comparison operator flips
    (r"===", "!==", "flip-eq"),
    (r"!==", "===", "flip-neq"),
    (r"(?<![<>=!])<(?![=])", ">", "flip-lt-gt"),
    (r"(?<![<>=!])>(?![=])", "<", "flip-gt-lt"),
    (r"<=", ">=", "flip-le-ge"),
    (r">=", "<=", "flip-ge-le"),
    # Boolean constants
    (r"\btrue\b", "false", "true-to-false"),
    (r"\bfalse\b", "true", "false-to-true"),
    # Logical operator flips
    (r"\&\&", "||", "flip-and-or"),
    (r"\|\|", "&&", "flip-or-and"),
    # Arithmetic mutations
    (r"(?<!\+)\+(?!\+|=)", "-", "plus-to-minus"),
    (r"(?<!-)-(?!-|=)", "+", "minus-to-plus"),
    # Assertion inversion (drop the assertion by negating)
    (r"console\.assert\(", "console.assert(!", "negate-assertions"),
]


def classify_exit(stdout: str, rc: int) -> str:
    """Classify a cbmc run result."""
    if "VERIFICATION SUCCESSFUL" in stdout:
        return "SUCCESSFUL"
    if "VERIFICATION FAILED" in stdout:
        return "FAILED"
    if rc == 0:
        return "SUCCESSFUL"
    return "ERROR"


def read_test_desc(desc: Path):
    """Parse test.desc. Returns (status, cbmc_args_list)."""
    lines = desc.read_text().splitlines()
    if len(lines) < 3:
        return None
    status = lines[0].strip()
    args = lines[2].strip().split() if len(lines) > 2 else []
    return status, args


def run_cbmc(cbmc: str, ts_file: Path, args: list, timeout: int) -> tuple:
    """Run cbmc with resource limits and return (stdout, rc)."""
    cmd = (
        f"ulimit -v 4000000 -t {timeout + 10}; "
        f"{cbmc} {' '.join(args)} {ts_file}"
    )
    try:
        result = subprocess.run(
            ["bash", "-c", cmd],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return result.stdout + result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return "TIMEOUT", -1


def mutate_test(test_dir: Path, cbmc: str, timeout: int) -> dict:
    """Run mutation tests against a single test directory."""
    desc_path = test_dir / "test.desc"
    ts_path = test_dir / "main.ts"
    if not (desc_path.exists() and ts_path.exists()):
        return {"status": "missing-files"}
    parsed = read_test_desc(desc_path)
    if parsed is None:
        return {"status": "bad-desc"}
    desc_status, cbmc_args = parsed
    if desc_status != "CORE":
        return {"status": "skipped-non-core"}

    original_src = ts_path.read_text()

    applicable = []
    caught = []
    tautological = []

    for pattern, replacement, name in MUTATIONS:
        if not re.search(pattern, original_src):
            continue
        applicable.append(name)
        mutated_src = re.sub(pattern, replacement, original_src, count=1)
        if mutated_src == original_src:
            continue
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".ts", delete=False
        ) as f:
            f.write(mutated_src)
            mutant = Path(f.name)
        try:
            stdout, rc = run_cbmc(cbmc, mutant, cbmc_args, timeout)
            outcome = classify_exit(stdout, rc)
            if outcome == "SUCCESSFUL":
                tautological.append(name)
            elif outcome == "FAILED":
                caught.append(name)
            # ERROR / TIMEOUT → skip
        finally:
            mutant.unlink(missing_ok=True)

    return {
        "status": "tested",
        "applicable": applicable,
        "caught": caught,
        "tautological": tautological,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tests",
        nargs="*",
        help="Specific test directories (default: all CORE tests)",
    )
    parser.add_argument(
        "--cbmc", default=str(DEFAULT_CBMC), help="Path to cbmc binary"
    )
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument(
        "--regression-dir",
        default=str(DEFAULT_REGRESSION),
        help="Root of typescript regression tests",
    )
    parser.add_argument(
        "--only-new",
        action="store_true",
        help="Only test directories containing 'symbolic' or soundness-review era tests",
    )
    parser.add_argument(
        "--verbose", action="store_true",
    )
    args = parser.parse_args()

    if not Path(args.cbmc).exists():
        print(f"ERROR: cbmc not found at {args.cbmc}", file=sys.stderr)
        sys.exit(1)

    # Resolve test directories.
    if args.tests:
        test_dirs = [Path(args.regression_dir) / name for name in args.tests]
    else:
        test_dirs = sorted(
            d for d in Path(args.regression_dir).iterdir() if d.is_dir()
        )

    if args.only_new:
        # Approximation: tests whose names match the 2026-05-07/08 sessions.
        keywords = [
            "symbolic", "typeof-literal", "string-concat-number",
            "object-destructuring-rename", "array-is-array",
            "math-max-min", "math-trunc", "math-round-spec",
            "math-constants-full", "math-trig-extra",
            "number-is-integer", "number-parse", "number-to-fixed",
            "number-to-string", "string-pad", "string-trim",
            "string-substring", "string-indexof", "string-starts-ends",
            "string-concat-method", "string-last-index", "string-empty",
            "array-slice-negative", "array-splice", "array-fill",
            "array-find-last", "array-last-index", "array-indexof-fromindex",
            "array-of", "heterogeneous-tuple", "optional-chaining",
            "map-delete", "set-dedup", "map-set-symbolic",
        ]
        test_dirs = [
            d for d in test_dirs
            if any(k in d.name for k in keywords)
        ]

    summary = {"tested": 0, "tautological": 0, "no-applicable": 0,
               "skipped": 0}
    tautological_tests = []

    for td in test_dirs:
        result = mutate_test(td, args.cbmc, args.timeout)
        status = result["status"]
        if status == "skipped-non-core":
            summary["skipped"] += 1
            continue
        if status != "tested":
            summary["skipped"] += 1
            if args.verbose:
                print(f"[{td.name}] SKIP ({status})")
            continue
        summary["tested"] += 1
        if not result["applicable"]:
            summary["no-applicable"] += 1
            if args.verbose:
                print(f"[{td.name}] no applicable mutations")
            continue
        if not result["caught"]:
            summary["tautological"] += 1
            tautological_tests.append(
                (td.name, result["tautological"])
            )
            print(
                f"[{td.name}] TAUTOLOGICAL — "
                f"{len(result['applicable'])} mutations applied, "
                f"none caught"
            )
        else:
            if args.verbose:
                print(
                    f"[{td.name}] OK "
                    f"({len(result['caught'])}/{len(result['applicable'])} "
                    f"mutations caught)"
                )

    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    for k, v in summary.items():
        print(f"  {k:20} {v:>5}")
    if tautological_tests:
        print("\nTautological tests (warrant review):")
        for name, muts in tautological_tests:
            print(f"  {name}")
            for m in muts[:3]:
                print(f"    - mutation '{m}' did not cause test to fail")
        sys.exit(1)
    else:
        print("\nNo tautological tests detected.")


if __name__ == "__main__":
    main()
