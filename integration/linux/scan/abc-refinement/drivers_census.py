#!/usr/bin/env python3
"""Kernel-wide drivers/* candidate census via per-leaf DBs (parallel, resumable).

For each drivers/<leaf> in the 6.12 broad config: build a per-leaf CodeQL DB
(cached) and run the 8 threat finders on it (the DB *is* the leaf, so every
base relation is cheap -- the dependable scoping mechanism).  Records per-leaf
per-asset counts, per-query status (ok/timeout/error), and the
HIGH-confidence count/index WRITE candidates (the likely-OOB-write triage
shortlist).  Resumable: a leaf whose JSON exists is skipped.

  drivers_census.py --build [--workers N]   # build+eval all leaves
  drivers_census.py --report                # aggregate cached JSONs
"""
import argparse
import json
import multiprocessing as mp
import os
import re
import subprocess
import zipfile

import outcome_summary as osum

HERE = os.path.dirname(os.path.abspath(__file__))
SCAN = os.path.dirname(HERE)
TREE = "/home/ubuntu/linux_6_12"
DRIVERS_DB = "/tmp/612-drivers-db"
OUT = "/tmp/dcensus"
BUILD_TIMEOUT = 1500
Q_TIMEOUT = 180


def leaves():
    z = zipfile.ZipFile(DRIVERS_DB + "/src.zip")
    pat = re.compile(r"drivers/([a-z0-9_]+)/")
    return sorted({pat.search(n).group(1) for n in z.namelist()
                   if n.endswith(".c") and pat.search(n)})


def leaf_db(leaf):
    return f"/tmp/leaf-drivers-{leaf}-db"


def build_leaf(leaf):
    db = leaf_db(leaf)
    if os.path.isdir(db) and os.path.isfile(db + "/src.zip"):
        return "cached"
    cmd = ["bash", "-c",
           f"ulimit -v 96000000; timeout {BUILD_TIMEOUT} "
           f"{SCAN}/build-codeql-db.sh {TREE} {db} drivers/{leaf}/ 16 "
           f">/dev/null 2>&1"]
    subprocess.run(cmd)
    if os.path.isfile(db + "/src.zip"):
        return "built"
    return "build-fail"


def eval_leaf(leaf):
    """Build + run finders; return a result dict.  Cached via JSON."""
    jpath = f"{OUT}/{leaf}.json"
    if os.path.isfile(jpath):
        return json.load(open(jpath))
    res = {"leaf": leaf, "build": build_leaf(leaf), "assets": {},
           "high_write": []}
    if not os.path.isfile(leaf_db(leaf) + "/src.zip"):
        json.dump(res, open(jpath, "w"))
        return res
    db = leaf_db(leaf)
    for label, q, mfield, mtok in osum.ASSETS:
        status, lines = osum.run_query(db, q, Q_TIMEOUT)
        a = {"status": status, "raw": len(lines), "mitigated": 0,
             "genuine": 0}
        if status == "ok":
            if mfield:
                a["mitigated"] = sum(1 for l in lines
                                     if osum.field(l, mfield) == mtok)
                a["genuine"] = a["raw"] - a["mitigated"]
            else:
                a["genuine"] = a["raw"]
            if q == "tainted_count_into_fixed_array.ql":
                for l in lines:
                    if "confidence=HIGH" in l and "impact=WRITE" in l:
                        res["high_write"].append(l)
        res["assets"][label] = a
    json.dump(res, open(jpath, "w"))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--workers", type=int, default=4)
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    ls = leaves()

    if a.build:
        # pre-compile the finders once (serial) so parallel workers hit the
        # compile cache rather than racing on first-compile.
        osum.run_query(leaf_db("nfc"), "tainted_count_into_fixed_array.ql", 60) \
            if os.path.isdir(leaf_db("nfc")) else None
        with mp.Pool(a.workers) as p:
            for i, r in enumerate(p.imap_unordered(eval_leaf, ls), 1):
                done = sum(v["status"] == "ok"
                           for v in r["assets"].values())
                print(f"[{i}/{len(ls)}] {r['leaf']:<14} build={r['build']:<10} "
                      f"finders_ok={done}/8 high_write={len(r['high_write'])}",
                      flush=True)

    if a.report or not a.build:
        report(ls)


def report(ls):
    rows = []
    for leaf in ls:
        jp = f"{OUT}/{leaf}.json"
        if os.path.isfile(jp):
            rows.append(json.load(open(jp)))
    print(f"\n# drivers/* candidate census ({len(rows)}/{len(ls)} leaves "
          f"evaluated)\n")
    tot = {}
    hw = []
    to = 0
    nbuilt = 0
    for r in rows:
        if not r["assets"]:
            continue
        nbuilt += 1
        for label, a in r["assets"].items():
            d = tot.setdefault(label, {"raw": 0, "mit": 0, "gen": 0,
                                       "to": 0, "err": 0})
            d["raw"] += a["raw"]
            d["mit"] += a["mitigated"]
            d["gen"] += a["genuine"]
            d["to"] += 1 if a["status"] == "timeout" else 0
            d["err"] += 1 if a["status"] == "error" else 0
        hw += r["high_write"]
    print(f"leaves with a built DB: {nbuilt}\n")
    print(f"{'asset':<22}{'raw':<8}{'mitigated':<11}{'genuine':<9}"
          f"{'q-TO':<6}{'q-err':<6}")
    print("-" * 62)
    traw = tgen = 0
    for label, _, _, _ in osum.ASSETS:
        d = tot.get(label)
        if not d:
            continue
        traw += d["raw"]
        tgen += d["gen"]
        print(f"{label:<22}{d['raw']:<8}{d['mit']:<11}{d['gen']:<9}"
              f"{d['to']:<6}{d['err']:<6}")
    print("-" * 62)
    print(f"{'TOTAL':<22}{traw:<8}{'':<11}{tgen:<9}")
    print(f"\nHIGH-confidence count/index WRITE candidates "
          f"(likely OOB-write): {len(hw)}")
    for l in hw:
        p = l.split("|")
        print(f"  {p[0]:<32} {p[1] if len(p)>1 else ''}:"
              f"{p[2] if len(p)>2 else ''}  {p[-1]}")


if __name__ == "__main__":
    main()
