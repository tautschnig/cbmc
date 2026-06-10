#!/usr/bin/env python3
"""Outcome distribution across the surveyed subsystems.

Runs the 8 threat-model finders over a set of CodeQL DBs and tallies what
OUTCOMES we actually see, at two levels:

  finder level  -- per (db, asset): COMPLETED vs QUERY-TIMEOUT, and for the
                   candidates found, MITIGATED (cleared by a recognised
                   mitigation: memset / clamp / nonzero / shift-bound /
                   capability gate) vs GENUINE (survivor needing discharge).

The point: coverage is breadth; this is what the finders *say* about the
covered code.  Most candidates clear via recognised mitigations; a smaller
genuine residual is what a CBMC discharge would chew on (and there, large
functions time out -- tracked separately by pipeline_eval.py).

Usage:
  outcome_summary.py --dbs "/tmp/net-all-db /tmp/fs-all-db ..." [--timeout 240]
"""
import argparse
import csv
import os
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CODEQL = "/home/ubuntu/codeql/codeql"
PACKS = "/home/ubuntu/codeql/qlpacks"

# (label, query, mitigation-field, mitigated-token); None => structural
# finder with no auto-mitigation column (every hit is a "genuine" candidate).
ASSETS = [
    ("A1-mem count/index", "tainted_count_into_fixed_array.ql", None, None),
    ("A1-mem decoded-len", "decoded_len_arith_overflow.ql", None, None),
    ("A1-mem skb/cursor", "skb_field_before_lencheck.ql", None, None),
    ("A1-UB div/shift", "tainted_ub_arith.ql", "mitigation", "MITIGATED"),
    ("A2 confidentiality", "infoleak_uninit_to_user.ql", "mitigation", "MITIGATED"),
    ("A3 integrity/CFI", "a3_tainted_fnptr.ql", None, None),
    ("A4 availability", "av_unbounded.ql", "mitigation", "MITIGATED"),
    ("A5 authorization", "auth_missing_capable.ql", "verdict", "CALLER-GUARDED"),
]


def field(line, key):
    for f in line.split("|"):
        if f.startswith(key + "="):
            return f.split("=", 1)[1]
    return ""


def run_query(db, query, timeout):
    """Return (status, lines).  status in {ok, timeout, error}."""
    env = dict(os.environ, PATH=f"/home/ubuntu/codeql:{os.environ['PATH']}",
               CODEQL_ALLOW_INSTALLATION_ANYWHERE="true")
    bqrs = tempfile.mktemp(suffix=".bqrs")
    csvf = tempfile.mktemp(suffix=".csv")
    try:
        subprocess.run(
            [CODEQL, "query", "run", f"--database={db}",
             f"--additional-packs={PACKS}", f"--output={bqrs}",
             os.path.join(HERE, query)],
            check=True, env=env, timeout=timeout,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return "timeout", []
    except subprocess.CalledProcessError:
        return "error", []
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
    return "ok", lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dbs", required=True, help="space-separated DB paths")
    ap.add_argument("--timeout", type=int, default=240)
    a = ap.parse_args()
    dbs = a.dbs.split()

    agg = {label: {"raw": 0, "mitigated": 0, "genuine": 0,
                   "completed": 0, "timeout": 0, "error": 0}
           for label, *_ in ASSETS}

    for db in dbs:
        name = os.path.basename(db)
        for label, q, mfield, mtok in ASSETS:
            status, lines = run_query(db, q, a.timeout)
            r = agg[label]
            if status == "timeout":
                r["timeout"] += 1
                print(f"  {name:<18}{label:<22}QUERY-TIMEOUT")
                continue
            if status == "error":
                r["error"] += 1
                print(f"  {name:<18}{label:<22}QUERY-ERROR")
                continue
            r["completed"] += 1
            raw = len(lines)
            r["raw"] += raw
            if mfield:
                mit = sum(1 for l in lines if field(l, mfield) == mtok)
                r["mitigated"] += mit
                r["genuine"] += raw - mit
            else:
                r["genuine"] += raw
            print(f"  {name:<18}{label:<22}raw={raw}")

    print("\n## Aggregate outcome distribution "
          f"({len(dbs)} DBs x {len(ASSETS)} finders)\n")
    print(f"{'asset':<22}{'raw':<7}{'MITIGATED':<11}{'GENUINE':<9}"
          f"{'q-ok':<6}{'q-TO':<6}{'q-err':<6}")
    print("-" * 67)
    tot = {k: 0 for k in ("raw", "mitigated", "genuine",
                          "completed", "timeout", "error")}
    for label, *_ in ASSETS:
        r = agg[label]
        for k in tot:
            tot[k] += r[k]
        print(f"{label:<22}{r['raw']:<7}{r['mitigated']:<11}{r['genuine']:<9}"
              f"{r['completed']:<6}{r['timeout']:<6}{r['error']:<6}")
    print("-" * 67)
    print(f"{'TOTAL':<22}{tot['raw']:<7}{tot['mitigated']:<11}"
          f"{tot['genuine']:<9}{tot['completed']:<6}{tot['timeout']:<6}"
          f"{tot['error']:<6}")
    rawn = tot["raw"] or 1
    print(f"\n  candidate outcomes: {tot['mitigated']} mitigated/cleared "
          f"({100*tot['mitigated']//rawn}%), {tot['genuine']} genuine "
          f"({100*tot['genuine']//rawn}%) of {tot['raw']} raw")
    print(f"  finder-run outcomes: {tot['completed']} completed, "
          f"{tot['timeout']} query-timeout, {tot['error']} error")


if __name__ == "__main__":
    main()
