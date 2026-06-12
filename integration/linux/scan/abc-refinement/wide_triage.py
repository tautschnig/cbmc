#!/usr/bin/env python3
"""Kernel-wide count/index OOB-write triage with the validated advisories.

Runs tainted_count_into_fixed_array.ql over every cached per-leaf CodeQL DB
(all classes), collects ALL count/index candidates, and classifies each by
the advisory-based triage:

  LOCALLY-BOUNDED  -- any local bound advisory present (adv_guard=GUARDED |
                      adv_mask=MASKED | adv_typebound=TYPEBOUND |
                      adv_srcguard=SRCGUARDED | adv_inv=MODULO |
                      adv_enum=yes | adv_bound=bareparam[caller's job]);
  GENUINE-FRONTIER -- none of the above (UNGUARDED, no recognised local
                      bound) -> needs a non-local validator or is a real
                      candidate.

This is the wide picture of how well-defended the count/index OOB-write
surface is.  Parallel + resumable (per-DB candidate cache).

  wide_triage.py --run [--workers N]
  wide_triage.py --report
"""
import argparse
import glob
import json
import multiprocessing as mp
import os

import triage_loop as tl

OUT = "/tmp/wtriage"
LOCAL_BOUND = ["adv_guard=GUARDED", "adv_mask=MASKED", "adv_typebound=TYPEBOUND",
               "adv_srcguard=SRCGUARDED", "adv_inv=MODULO", "adv_enum=yes",
               "adv_bound=bareparam"]


def dbs():
    return sorted(d for d in glob.glob("/tmp/leaf-*-db")
                  if os.path.isfile(d + "/src.zip"))


def run_db(db):
    key = os.path.basename(db)
    jp = f"{OUT}/{key}.json"
    if os.path.isfile(jp):
        return json.load(open(jp))
    try:
        lines = tl.run_oracle(db, "tainted_count_into_fixed_array.ql")
    except Exception:
        lines = None
    res = {"db": key, "ok": lines is not None, "lines": lines or []}
    json.dump(res, open(jp, "w"))
    return res


def classify(line):
    local = any(t in line for t in LOCAL_BOUND)
    return "LOCALLY-BOUNDED" if local else "GENUINE-FRONTIER"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--workers", type=int, default=8)
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    ds = dbs()
    if a.run:
        with mp.Pool(a.workers) as p:
            for i, r in enumerate(p.imap_unordered(run_db, ds), 1):
                print(f"[{i}/{len(ds)}] {r['db']:<40} "
                      f"{'ok' if r['ok'] else 'ERR'} cands={len(r['lines'])}",
                      flush=True)
    if a.report or not a.run:
        report(ds)


def report(ds):
    from collections import Counter
    cls = Counter()
    impact = Counter()
    conf = Counter()
    adv = Counter()
    n_ok = total = 0
    seen = set()
    for db in ds:
        jp = f"{OUT}/{os.path.basename(db)}.json"
        if not os.path.isfile(jp):
            continue
        r = json.load(open(jp))
        if not r["ok"]:
            continue
        n_ok += 1
        for l in r["lines"]:
            p = l.split("|")
            sig = (p[0], p[2] if len(p) > 2 else "")  # func, line: dedup
            if sig in seen:
                continue
            seen.add(sig)
            total += 1
            cls[classify(l)] += 1
            impact["WRITE" if "impact=WRITE" in l else "READ"] += 1
            conf["HIGH" if "confidence=HIGH" in l else "MEDIUM"] += 1
            for t in LOCAL_BOUND:
                if t in l:
                    adv[t] += 1
    print(f"\n# kernel-wide count/index triage ({n_ok}/{len(ds)} leaf DBs, "
          f"{total} distinct candidates)\n")
    print("## triage class")
    for k, c in cls.most_common():
        print(f"  {k:<18}{c}  ({100*c//max(total,1)}%)")
    print("\n## impact / confidence")
    for k, c in list(impact.items()) + list(conf.items()):
        print(f"  {k:<18}{c}")
    print("\n## local-bound advisory hits (why LOCALLY-BOUNDED)")
    for k, c in adv.most_common():
        print(f"  {k:<26}{c}")
    g = cls.get("GENUINE-FRONTIER", 0)
    hw = sum(1 for db in ds
             if os.path.isfile(f"{OUT}/{os.path.basename(db)}.json")
             for l in json.load(open(f"{OUT}/{os.path.basename(db)}.json"))["lines"]
             if "confidence=HIGH" in l and "impact=WRITE" in l
             and classify(l) == "GENUINE-FRONTIER")
    print(f"\n  GENUINE-FRONTIER total: {g}; of which HIGH-confidence WRITE "
          f"(top bug-hunt priority): {hw}")


if __name__ == "__main__":
    main()
