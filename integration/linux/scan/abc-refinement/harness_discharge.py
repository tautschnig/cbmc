#!/usr/bin/env python3
"""Auto caller-precondition harness discharge for the VIOLATED set.

The raw `--function` survey showed VIOLATED verdicts are largely nondet-INPUT
artifacts (input struct pointers become dynamic objects of unknown size).
This re-discharges each VIOLATED candidate through a goto-harness that
ALLOCATES the input pointers to CONCRETE objects (so array fields get their
real sizes), removing the artifact -- then classifies the real verdict.

Honest expectation: harnessing fixes soundness but exposes full-function
complexity, so many still TIMEOUT; the ones that complete give a real verdict
(PROVED = locally safe; VIOLATED = genuine/non-local-validator OOB).  The
verbatim-slice approach remains preferable (concrete AND small).

  harness_discharge.py            # over all VIOLATED in /tmp/disc/*.v
"""
import glob
import json
import os
import subprocess

BUILD = "/home/ubuntu/cbmc-github.git/build/bin"
GH, CBMC = f"{BUILD}/goto-harness", f"{BUILD}/cbmc"
TIMEOUT = int(os.environ.get("HD_TIMEOUT", "120"))


def sh(c):
    return subprocess.run(["bash", "-c", c], capture_output=True, text=True)


def harness_discharge(gb, func):
    hgb = f"/tmp/disc/_h_{func}.gb"
    g = sh(f"ulimit -v 48000000; timeout 120 {GH} {gb} {hgb} "
           f"--harness-function-name __h --harness-type call-function "
           f"--function {func} --min-null-tree-depth 2 --max-nondet-tree-depth 2 "
           f"--max-array-size 4 2>&1")
    if not os.path.isfile(hgb):
        return "HARNESS-FAIL"
    r = sh(f"ulimit -v 48000000; timeout {TIMEOUT} {CBMC} {hgb} --function __h "
           f"--bounds-check --object-bits 16 --unwind 3 --partial-loops "
           f"--no-unwinding-assertions 2>&1")
    try:
        os.unlink(hgb)
    except OSError:
        pass
    if r.returncode == 124:
        return "TIMEOUT"
    out = r.stdout
    if "VERIFICATION SUCCESSFUL" in out:
        return "PROVED"
    if "VERIFICATION FAILED" in out:
        return "VIOLATED"
    if "too many addressed objects" in out:
        return "OBJECT-BITS"
    return "ERROR"


def main():
    viol = []
    for f in glob.glob("/tmp/disc/*.v"):
        r = json.load(open(f))
        if r.get("verdict") == "VIOLATED":
            viol.append(r)
    print(f"# auto-harness discharge of {len(viol)} VIOLATED candidates "
          f"(concrete-input harness)\n")
    from collections import Counter
    out = Counter()
    for r in sorted(viol, key=lambda r: r["func"]):
        gb = "/tmp/disc/" + r["tu"].replace("/", "__")[:-2] + ".gb"
        v = harness_discharge(gb, r["func"]) if os.path.isfile(gb) else "NO-GB"
        out[v] += 1
        print(f"  {r['func']:<40}{r['tu'].split('/')[-1]:<22}raw=VIOLATED -> "
              f"harnessed={v}", flush=True)
    print("\n## harnessed-discharge distribution")
    for k, c in out.most_common():
        print(f"  {k:<14}{c}")


if __name__ == "__main__":
    main()
