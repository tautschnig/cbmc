#!/usr/bin/env python3
"""Adjudicate a whole sweep: one verdict per runner, with reasons.

run_all.sh already reported a verdict, an obligation count, a witness-failure
count, nobody and unknown. Three things it could not catch:

  * a witness that PASSED. It counted witness failures, so a harness with two
    witnesses of which one passed showed `witnesses_fail=1` and nothing
    complained -- yet a passing witness is the signal that the state the
    property describes may be unreachable and the proof empty.
  * a harness with NO witness at all, which showed `witnesses_fail=0` and
    looked indistinguishable from a clean run.
  * a failing unwinding assertion, which the runners print but the summary
    did not read.

This reads the captured per-runner output and applies the same rules as
check_hygiene.sh, over the corpus rather than one log.

Usage: check_sweep.py LOGDIR [--expect-fail name,name,...]
Exit 0 if every runner's claim stands, 1 otherwise.
"""
import argparse
import os
import re
import sys


def adjudicate(text, expect_fail):
    """-> (ok, [reasons]) for one runner's captured output."""
    reasons = []

    verdicts = re.findall(r"^VERIFICATION ([A-Z]+)", text, re.M)
    if not verdicts:
        reasons.append("no verdict at all (timeout, build failure, or a "
                       "solver binary that could not be executed)")
    if "BUILD FAILED" in text:
        reasons.append("a build failed, so some result came from no binary")

    # the runners print these counters themselves
    for label, pat in (("UNKNOWN", r"UNKNOWN:\s*([0-9]+)"),
                       ("reached bodyless callee",
                        r"no-?body(?: reached)?:\s*([0-9]+)"),
                       ("failing unwinding assertion",
                        r"unwind FAIL:\s*([0-9]+)")):
        vals = [int(v) for v in re.findall(pat, text)]
        if vals and max(vals) > 0:
            reasons.append(f"{max(vals)} {label}")

    other = [int(v) for v in re.findall(r"other FAILURE:\s*([0-9]+)", text)]
    if other and max(other) > 0 and not expect_fail:
        reasons.append(f"{max(other)} unexpected failing property")

    # Any label mentioning a witness.  Matching only "vacuity witness"
    # missed harness_rx_reqid.c, whose label is just "witness: ...".
    wit = re.findall(r"^.*witness.*: (SUCCESS|FAILURE)$", text, re.M)
    if not wit:
        reasons.append("NO vacuity witness: a pass here cannot be "
                       "distinguished from the property's state never arising")
    elif "SUCCESS" in wit:
        n = wit.count("SUCCESS")
        reasons.append(f"{n} vacuity witness(es) did NOT fail: the state they "
                       f"describe may be unreachable, so the proof can be empty")

    if expect_fail:
        if "FAILED" not in verdicts:
            reasons.append("marked EXPECTED-FAIL but nothing failed: it no "
                           "longer reproduces the finding it claims to")
    elif verdicts and set(verdicts) != {"SUCCESSFUL"}:
        bad = sorted(set(verdicts) - {"SUCCESSFUL"})
        reasons.append(f"verdict {', '.join(bad)} but not marked EXPECTED-FAIL")

    return (not reasons), reasons


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logdir")
    ap.add_argument("--expect-fail", default="")
    args = ap.parse_args()
    expected = {s.strip() for s in args.expect_fail.split(",") if s.strip()}

    if not os.path.isdir(args.logdir):
        print(f"no such log directory: {args.logdir}", file=sys.stderr)
        return 1

    names = sorted(f[:-4] for f in os.listdir(args.logdir)
                   if f.endswith(".txt"))
    if not names:
        print(f"no runner output in {args.logdir}", file=sys.stderr)
        return 1

    bad = []
    print(f"{'runner':<20} {'claim':<14} reasons")
    print("-" * 78)
    for n in names:
        text = open(os.path.join(args.logdir, n + ".txt"),
                    errors="replace").read()
        ok, reasons = adjudicate(text, n in expected)
        tag = "STANDS" if ok else "NOT SUPPORTED"
        kind = "expected-fail" if n in expected else "proved"
        print(f"{n:<20} {kind:<14} {tag}")
        for r in reasons:
            print(f"{'':<20} {'':<14}   - {r}")
        if not ok:
            bad.append(n)

    print()
    print(f"{len(names) - len(bad)} of {len(names)} runner claims stand")
    if bad:
        print("not supported: " + ", ".join(bad))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
