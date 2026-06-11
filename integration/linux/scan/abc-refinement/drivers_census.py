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

Collector-architecture contract (doc/collector-architecture-2026-06.md):
the finders are a SOUND over-approximate collector; the mitigated /
genuine / resolved / distilled splits below are ADVISORY tiers over the
FULL retained candidate set -- they rank, they never drop.  Only CBMC, a
provably-sound static check, or manual review may remove a candidate.
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
Q_TIMEOUT = int(os.environ.get("DCENSUS_Q_TIMEOUT", "180"))


def leaves():
    z = zipfile.ZipFile(DRIVERS_DB + "/src.zip")
    pat = re.compile(r"drivers/([a-z0-9_]+)/")
    return sorted({pat.search(n).group(1) for n in z.namelist()
                   if n.endswith(".c") and pat.search(n)})


# Heavy leaves whose per-leaf DB is too big for the (slower) sound
# over-approximate collector at the per-query budget -- expanded into
# per-depth-1-subdir units so every unit completes.
HEAVY = {"media", "gpu", "net", "staging", "usb", "scsi", "infiniband",
         "iio", "clk"}


def subdirs(leaf):
    p = os.path.join(TREE, "drivers", leaf)
    if not os.path.isdir(p):
        return []
    return sorted(d for d in os.listdir(p)
                  if os.path.isdir(os.path.join(p, d)))


def units():
    """(target, db, key) for every unit; heavy leaves are expanded into
    per-subdir units so the sound (slower) collector completes.  Flat leaves
    are yielded FIRST (their per-leaf DBs already exist -> they re-baseline
    fast), heavy sub-units second (they pay a build cost)."""
    flat = []
    heavy = []
    for leaf in leaves():
        if leaf in HEAVY and subdirs(leaf):
            for s in subdirs(leaf):
                heavy.append((f"drivers/{leaf}/{s}/",
                              f"/tmp/leaf-drivers-{leaf}-{s}-db", f"{leaf}__{s}"))
        else:
            flat.append((f"drivers/{leaf}/",
                         f"/tmp/leaf-drivers-{leaf}-db", leaf))
    return flat + heavy


def build_unit(target, db):
    if os.path.isdir(db) and os.path.isfile(db + "/src.zip"):
        return "cached"
    cmd = ["bash", "-c",
           f"ulimit -v 96000000; timeout {BUILD_TIMEOUT} "
           f"{SCAN}/build-codeql-db.sh {TREE} {db} {target} 16 >/dev/null 2>&1"]
    subprocess.run(cmd)
    return "built" if os.path.isfile(db + "/src.zip") else "build-fail"


def eval_unit(unit):
    """Build + run finders on one unit; cached via JSON.  `high` collects all
    confidence=HIGH count/index candidate lines (with advisories) for the
    ranking layer."""
    target, db, key = unit
    jpath = f"{OUT}/{key}.json"
    if os.path.isfile(jpath):
        return json.load(open(jpath))
    res = {"leaf": key, "build": build_unit(target, db), "assets": {},
           "high": []}
    if not os.path.isfile(db + "/src.zip"):
        json.dump(res, open(jpath, "w"))
        return res
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
                    if "confidence=HIGH" in l:
                        res["high"].append(l)
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
    us = list(units())

    if a.build:
        # pre-compile the finders once (serial) so parallel workers hit the
        # compile cache rather than racing on first-compile.
        nfc = "/tmp/leaf-drivers-nfc-db"
        if os.path.isdir(nfc):
            osum.run_query(nfc, "tainted_count_into_fixed_array.ql", 60)
        with mp.Pool(a.workers) as p:
            for i, r in enumerate(p.imap_unordered(eval_unit, us), 1):
                done = sum(v["status"] == "ok"
                           for v in r["assets"].values())
                print(f"[{i}/{len(us)}] {r['leaf']:<22} build={r['build']:<10} "
                      f"finders_ok={done}/8 high={len(r.get('high', []))}",
                      flush=True)

    if a.report or not a.build:
        report(us)


def report(us):
    rows = []
    for _, _, key in us:
        jp = f"{OUT}/{key}.json"
        if os.path.isfile(jp):
            rows.append(json.load(open(jp)))
    print(f"\n# drivers/* candidate census ({len(rows)}/{len(us)} units "
          f"evaluated)\n")
    tot = {}
    hi = []
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
        hi += r.get("high", [])
        # backward-compat with older high_write-only JSONs
        hi += [l for l in r.get("high_write", []) if l not in hi]
    print(f"units with a built DB: {nbuilt}\n")
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
    hw = [l for l in hi if "impact=WRITE" in l]
    print(f"\nHIGH-confidence count/index WRITE candidates -- a RANKING over "
          f"the full retained set, NOT a filtered subset ({len(hw)}):")
    print("  (advisories rank only; CBMC / sound check / manual review is the "
          "sole definitive filter)")
    for l in hw:
        p = l.split("|")
        adv = "|".join(x for x in p if x.startswith("adv_"))
        print(f"  {p[0]:<28} {p[1].split('/')[-1] if len(p)>1 else ''}:"
              f"{p[2] if len(p)>2 else '':<6} {adv}")


if __name__ == "__main__":
    main()
