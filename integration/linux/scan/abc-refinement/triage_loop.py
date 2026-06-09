#!/usr/bin/env python3
"""Closed-loop triage driver: CodeQL oracle hit -> CBMC verdict.

Runs a confidence/impact-tiered ABC oracle over a CodeQL database, then for
each candidate auto-generates a CBMC harness (via oracle_harness_gen.py),
runs CBMC on both the buggy and fixed shapes, and prints a triage table:

    function | kind | confidence | impact | CBMC(buggy) | CBMC(fixed)

This closes the stage-1 -> stage-2 loop the project was missing: instead
of a flat list of candidates, every candidate comes out with a
machine-checked reachability verdict plus the precision (confidence) and
exploitability (impact) tiers -- the triage signals the "kernel security
in the age of AI" talk identifies as the real bottleneck.

Honest scope: the harness models the bug SHAPE parameterised by the
candidate's extracted constants (array size, kind), not the verbatim
function body.  A FAILED buggy / SUCCESSFUL fixed pair confirms the shape
is genuinely OOB-reachable and that the canonical guard fixes it; it does
NOT prove the specific call site is reachable with attacker input (that is
the real-function-harness / cover-probe job, auto_harness.py's domain).

Usage:
    triage_loop.py --db /tmp/broad-next-db \\
        --query tainted_count_into_fixed_array.ql \\
        [--min-confidence HIGH] [--limit N]
"""
import argparse
import csv
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CBMC = os.path.normpath(os.path.join(HERE, "../../../../build/bin/cbmc"))
CODEQL = "/home/ubuntu/codeql/codeql"
PACKS = "/home/ubuntu/codeql/qlpacks"

# query filename -> (harness oracle type, cbmc flags, unwind)
ORACLES = {
    "tainted_count_into_fixed_array.ql": (
        "count", ["--bounds-check", "--pointer-check"], 10),
    "decoded_len_arith_overflow.ql": (
        "decoded", ["--unsigned-overflow-check"], 4),
    "skb_field_before_lencheck.ql": (
        "skb", ["--bounds-check", "--pointer-check"], 6),
}

CONF_RANK = {"HIGH": 2, "MEDIUM": 1, "": 0}


def run_oracle(db, query):
    """Run the CodeQL query, return the list of structured candidate lines."""
    env = dict(os.environ, PATH=f"/home/ubuntu/codeql:{os.environ['PATH']}",
               CODEQL_ALLOW_INSTALLATION_ANYWHERE="true")
    bqrs = tempfile.mktemp(suffix=".bqrs")
    csvf = tempfile.mktemp(suffix=".csv")
    subprocess.run(
        [CODEQL, "query", "run", f"--database={db}",
         f"--additional-packs={PACKS}", f"--output={bqrs}",
         os.path.join(HERE, query)],
        check=True, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(csvf, "w") as f:
        subprocess.run([CODEQL, "bqrs", "decode", "--format=csv", bqrs],
                       check=True, env=env, stdout=f, stderr=subprocess.DEVNULL)
    lines = []
    with open(csvf, newline="") as f:
        for row in csv.reader(f):
            if len(row) >= 2 and "|" in row[1]:
                lines.append(row[1])
    for p in (bqrs, csvf):
        try:
            os.unlink(p)
        except OSError:
            pass
    return lines


def parse_candidate(line):
    """Pull func, kind, confidence, impact out of a structured line."""
    p = line.split("|")
    d = {"raw": line, "func": p[0], "file": p[1],
         "line": p[2] if len(p) > 2 else "?",
         "kind": p[3] if len(p) > 3 else "?", "confidence": "", "impact": ""}
    for field in p:
        m = re.match(r"(confidence|impact)=(\w+)", field)
        if m:
            d[m.group(1)] = m.group(2)
    return d


def cbmc_verdict(harness, func, flags, unwind):
    try:
        out = subprocess.run(
            [CBMC, harness, "--function", func, *flags,
             "--unwind", str(unwind)],
            capture_output=True, text=True, timeout=90).stdout
    except subprocess.TimeoutExpired:
        return "TIMEOUT"
    for ln in out.splitlines():
        if ln.startswith("VERIFICATION"):
            return "FAILED" if "FAILED" in ln else "SUCCESSFUL"
    return "ERROR"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    ap.add_argument("--query", required=True, choices=list(ORACLES))
    ap.add_argument("--min-confidence", default="", choices=["", "MEDIUM", "HIGH"])
    ap.add_argument("--limit", type=int, default=0, help="0 = no limit")
    a = ap.parse_args()

    otype, flags, unwind = ORACLES[a.query]
    cands = [parse_candidate(l) for l in run_oracle(a.db, a.query)]
    if a.min_confidence:
        floor = CONF_RANK[a.min_confidence]
        cands = [c for c in cands if CONF_RANK.get(c["confidence"], 0) >= floor]
    # highest-confidence first, then write-impact first
    cands.sort(key=lambda c: (CONF_RANK.get(c["confidence"], 0),
                              c["impact"] == "WRITE"), reverse=True)
    if a.limit:
        cands = cands[:a.limit]

    print(f"# triage_loop: {a.query} on {a.db}")
    print(f"# {len(cands)} candidate(s)"
          f"{' (>= ' + a.min_confidence + ')' if a.min_confidence else ''}\n")
    hdr = ("function", "kind", "conf", "impact", "CBMC:buggy", "CBMC:fixed")
    print(f"{hdr[0]:<28}{hdr[1]:<18}{hdr[2]:<7}{hdr[3]:<7}"
          f"{hdr[4]:<12}{hdr[5]:<12}")
    print("-" * 84)
    for c in cands:
        h = tempfile.mktemp(suffix=".c")
        with open(h, "w") as f:
            subprocess.run(
                [sys.executable, os.path.join(HERE, "oracle_harness_gen.py"),
                 "--oracle", otype, "-c", c["raw"]],
                check=True, stdout=f)
        buggy = cbmc_verdict(h, "harness_buggy", flags, unwind)
        fixed = cbmc_verdict(h, "harness_fixed", flags, unwind)
        os.unlink(h)
        print(f"{c['func']:<28}{c['kind']:<18}{c['confidence']:<7}"
              f"{c['impact']:<7}{buggy:<12}{fixed:<12}")


if __name__ == "__main__":
    main()
