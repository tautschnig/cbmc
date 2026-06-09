#!/usr/bin/env python3
"""Cover-probe "assumption bisection" driver.

The talk describes how strong models validate a claimed path A->B->C->D to
a sink by sprinkling probes at the intermediate hops and checking which are
actually reached, bisecting to the first hop where the claim breaks.  This
is the CBMC analogue: a C file instrumented with CHECKPOINT(id, cond)
probes (see cover_probe.h) -- each carrying the assumption the claimed path
makes at that hop -- is run under `cbmc --cover cover`.  Every probe
becomes a coverage goal:

    SATISFIED  the hop is reachable AND its assumption is feasible there
    FAILED     the hop / assumption is infeasible

Walking the probes in source (path) order, the FIRST FAILED is exactly the
point where the claimed reasoning diverges from what the code allows.  This
upgrades a stage-2 verdict from "the bug SHAPE is reachable" to "this
specific precondition chain to the sink is / is not feasible".

Usage:
    cover_probe.py --file harness.c --function parse_claimed_overflow
                   [--include DIR]... [--unwind N]
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CBMC = os.path.normpath(os.path.join(HERE, "../../../../build/bin/cbmc"))

GOAL_RE = re.compile(
    r"\[(?P<id>\S+\.coverage\.\d+)\]\s+file\s+(?P<file>\S+)\s+line\s+"
    r"(?P<line>\d+)\s+function\s+(?P<func>\S+)\s+condition\s+'(?P<cond>.*)':\s+"
    r"(?P<verdict>SATISFIED|FAILED)")


def run_cover(cfile, func, includes, unwind):
    cmd = [CBMC, cfile, "-DCBMC_PROBE", "--function", func, "--cover", "cover"]
    for inc in includes:
        cmd += ["-I", inc]
    if unwind:
        cmd += ["--unwind", str(unwind)]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=300).stdout
    goals = []
    for m in GOAL_RE.finditer(out):
        goals.append({
            "id": m.group("id"), "line": int(m.group("line")),
            "func": m.group("func"), "cond": m.group("cond"),
            "reached": m.group("verdict") == "SATISFIED",
        })
    return goals, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True)
    ap.add_argument("--function", required=True)
    ap.add_argument("--probe-function", default=None,
                    help="function whose CHECKPOINT goals to report "
                    "(default: same as --function; set when the entry "
                    "calls a separate probe-bearing function)")
    ap.add_argument("--include", action="append", default=[],
                    help="extra -I include dir (repeatable)")
    ap.add_argument("--unwind", type=int, default=0)
    a = ap.parse_args()

    incs = [HERE] + a.include  # cover_probe.h lives next to this driver
    goals, raw = run_cover(a.file, a.function, incs, a.unwind)
    # keep only probes inside the function under analysis (CBMC instruments
    # the whole translation unit; goals in other functions are unreachable
    # from this entry point and would pollute the bisection)
    probe_fn = a.probe_function or a.function
    goals = [g for g in goals if g["func"] == probe_fn]
    if not goals:
        print("no coverage goals found -- is the file instrumented with "
              "CHECKPOINT()/REACH() and does the function name match?")
        print("--- cbmc output tail ---")
        print("\n".join(raw.splitlines()[-15:]))
        sys.exit(2)

    # path order = source line order, then goal index
    goals.sort(key=lambda g: (g["line"], int(g["id"].rsplit(".", 1)[1])))

    print(f"cover-probe bisection: {a.function} ({a.file})")
    first_blocked = None
    for g in goals:
        tag = "reached" if g["reached"] else "BLOCKED"
        mark = ""
        if not g["reached"] and first_blocked is None:
            first_blocked = g
            mark = "   <-- claimed path breaks here"
        print(f"  [{tag}] line {g['line']:<5} {g['cond']}{mark}")

    print()
    if first_blocked is None:
        print("VERDICT: every claimed hop is feasible -- the precondition "
              "chain to the sink is reachable as described.")
        sys.exit(0)
    else:
        print(f"VERDICT: claimed path is INFEASIBLE; first blocked hop at "
              f"line {first_blocked['line']} ({first_blocked['cond']}). "
              f"An earlier constraint rules out this assumption.")
        sys.exit(1)


if __name__ == "__main__":
    main()
