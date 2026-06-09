#!/usr/bin/env python3
"""Auto-generated verbatim reach harness via goto-harness.

Scales `src=REAL` from hand-authored harnesses to ANY function in a
buildable kernel translation unit.  Given the full-TU goto binary (built
once with goto-cc) and a target function, it uses CBMC's `goto-harness`
(--harness-type call-function) to synthesise a harness that nondet-
initialises and validly allocates the function's arguments, then runs CBMC
--bounds-check on the VERBATIM function body and reports whether a real
out-of-bounds property fails.

This is the "function in isolation" (worst-case / probe_vuln) view: args
are arbitrary, so a FAILURE means the body is OOB-reachable for some input.
Combined with caller_precondition.ql's verdict it gives the full triage
(isolation-OOB + UNGUARDED = genuine concern; isolation-OOB + GUARDED =
the caller/producer saves it) -- with NO manual harness authoring.

Usage:
  auto_real_harness.py --gb /tmp/gw.gb --function cgw_csum_crc8_pos
      [--unwind N] [--max-array-size N]
"""
import argparse
import os
import re
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../../../.."))
GOTO_HARNESS = os.path.join(ROOT, "build/bin/goto-harness")
CBMC = os.path.join(ROOT, "build/bin/cbmc")

# a genuine OOB / memory-safety failure in the target function (not an
# unwinding-assertion or NULL artefact)
OOB_RE = re.compile(
    r"\[(?P<fn>\w+)\.(?P<kind>array_bounds|pointer_dereference)\.\d+\].*"
    r"(upper bound|lower bound|outside object bounds).*: FAILURE")


def auto_verdict(gb, function, unwind=16, max_array=64, timeout=300):
    """Auto-generate a verbatim harness for `function` in goto binary `gb`
    and return (verdict, sites): verdict is 'REAL-OOB' / 'CLEAN' /
    'TIMEOUT' / 'HARNESS-FAIL'; sites is a list of (kind, line, property)."""
    hgb = tempfile.mktemp(suffix=".gb")
    hname = "h_" + function
    gh = subprocess.run(
        [GOTO_HARNESS, gb, hgb, "--harness-function-name", hname,
         "--harness-type", "call-function", "--function", function,
         "--min-null-tree-depth", "4", "--max-array-size", str(max_array)],
        capture_output=True, text=True)
    if not os.path.exists(hgb):
        return "HARNESS-FAIL", []
    try:
        out = subprocess.run(
            [CBMC, hgb, "--function", hname, "--bounds-check",
             "--pointer-check", "--no-unwinding-assertions",
             "--unwind", str(unwind)],
            capture_output=True, text=True, timeout=timeout).stdout
    except subprocess.TimeoutExpired:
        os.unlink(hgb)
        return "TIMEOUT", []
    os.unlink(hgb)
    sites = []
    for ln in out.splitlines():
        m = OOB_RE.search(ln)
        if m and m.group("fn") == function:
            loc = re.search(r"line \d+", ln)
            prop = re.search(r"\] (.*?): FAILURE", ln)
            sites.append((m.group("kind"),
                          loc.group(0) if loc else "?",
                          prop.group(1) if prop else ""))
    return ("REAL-OOB" if sites else "CLEAN"), sites


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gb", required=True, help="full-TU goto binary")
    ap.add_argument("--function", required=True)
    ap.add_argument("--unwind", type=int, default=16)
    ap.add_argument("--max-array-size", type=int, default=64)
    ap.add_argument("--timeout", type=int, default=300)
    a = ap.parse_args()
    verdict, sites = auto_verdict(a.gb, a.function, a.unwind,
                                  a.max_array_size, a.timeout)
    print(f"{a.function}: {verdict} (auto-harness, verbatim body)")
    for kind, loc, prop in sites[:6]:
        print(f"    {kind} @ {loc}: {prop}")


if __name__ == "__main__":
    main()
