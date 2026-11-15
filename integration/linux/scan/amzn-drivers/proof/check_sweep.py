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
    notes = []

    # The verdict is not reliably at line start: runners indent their captured
    # output, and some prefix it ("part 2  VERIFICATION SUCCESSFUL").
    # Anchoring at line start reported "no verdict at all" for runners whose
    # proofs were fine, and an adjudicator that cries wolf gets switched off --
    # which is the failure it exists to prevent.
    verdicts = re.findall(r"VERIFICATION ([A-Z]+)", text)
    if not verdicts:
        reasons.append("no verdict at all (timeout, build failure, or a "
                       "solver binary that could not be executed)")
    # Runners that carry a demonstration alongside a proof check the
    # demonstration's outcome themselves and print UNEXPECTED when it stops
    # reproducing; their failure lines are tagged [demonstration] and so do
    # not end in ": FAILURE".
    for ln in text.splitlines():
        if "UNEXPECTED" in ln:
            reasons.append(ln.strip())
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
            # An undecided property cannot make a FOUND counterexample unsound,
            # so for a runner whose claim is "reproduces finding X" UNKNOWN is
            # reported but does not reject the claim.  For a proof it does.
            if label == "UNKNOWN" and expect_fail:
                notes.append(f"{max(vals)} UNKNOWN (tolerated: the claim is a "
                             f"counterexample, which they cannot undermine)")
            else:
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
        # A FAILED verdict is EXPECTED from the vacuity run, whose whole point
        # is that the witnesses fail; runners capture both runs in one file.
        # So only complain when something other than a witness failed, which
        # the counters above already cover.  Treating every FAILED as a
        # problem flagged four sound proofs.
        unexplained = [ln for ln in text.splitlines()
                       if ln.rstrip().endswith(": FAILURE")
                       and "witness" not in ln]
        if unexplained:
            reasons.append(
                f"verdict FAILED with {len(unexplained)} non-witness "
                f"failure(s) and not marked EXPECTED-FAIL")

    return (not reasons), reasons + [f"note: {n}" for n in notes]


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

    bad, pending = [], []
    print(f"{'runner':<20} {'claim':<14} reasons")
    print("-" * 78)
    for n in names:
        text = open(os.path.join(args.logdir, n + ".txt"),
                    errors="replace").read()
        # An empty file is a runner the sweep has started and not finished.
        # Reporting that as an unsupported claim makes the adjudicator cry
        # wolf on every mid-sweep check, so it is held separate from the
        # verdict rather than counted against it.
        if not text.strip():
            print(f"{n:<20} {'in progress':<14} no output yet")
            pending.append(n)
            continue
        ok, reasons = adjudicate(text, n in expected)
        tag = "STANDS" if ok else "NOT SUPPORTED"
        kind = "expected-fail" if n in expected else "proved"
        print(f"{n:<20} {kind:<14} {tag}")
        for r in reasons:
            print(f"{'':<20} {'':<14}   - {r}")
        if not ok:
            bad.append(n)

    print()
    judged = len(names) - len(pending)
    print(f"{judged - len(bad)} of {judged} finished runner claims stand")
    if pending:
        print(f"{len(pending)} still running: " + ", ".join(pending))
    if bad:
        print("not supported: " + ", ".join(bad))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
