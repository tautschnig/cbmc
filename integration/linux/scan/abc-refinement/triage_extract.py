#!/usr/bin/env python3
"""Extract triage candidates across the cached drivers leaf DBs (parallel).

Runs a finder over every built /tmp/leaf-drivers-*-db and emits the matching
candidate lines (file:line + detail), so the next tier can be triaged.

  triage_extract.py count_high   # count/index, confidence=HIGH (the READs)
  triage_extract.py skb          # skb/cursor candidates
"""
import glob
import multiprocessing as mp
import os
import sys

import outcome_summary as osum

Q = {"count_high": "tainted_count_into_fixed_array.ql",
     "skb": "skb_field_before_lencheck.ql"}
TIMEOUT = 240


def leaf_dbs():
    return sorted(d for d in glob.glob("/tmp/leaf-drivers-*-db")
                  if os.path.isfile(d + "/src.zip"))


def run(args):
    db, query, mode = args
    status, lines = osum.run_query(db, query, TIMEOUT)
    out = []
    if status != "ok":
        return (db, status, [])
    for l in lines:
        if mode == "count_high" and "confidence=HIGH" not in l:
            continue
        out.append(l)
    return (db, status, out)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "count_high"
    query = Q[mode]
    dbs = leaf_dbs()
    tasks = [(db, query, mode) for db in dbs]
    alllines = []
    to = []
    with mp.Pool(6) as p:
        for db, status, lines in p.imap_unordered(run, tasks):
            leaf = db.split("leaf-drivers-")[1].rsplit("-db", 1)[0]
            if status != "ok":
                to.append(leaf)
            for l in lines:
                alllines.append((leaf, l))
    print(f"# {mode}: {len(alllines)} candidates over {len(dbs)} leaf DBs "
          f"({len(to)} finder-timeouts: {to})\n")
    for leaf, l in sorted(alllines):
        p = l.split("|")
        fn = p[0]
        loc = ""
        det = ""
        for i, f in enumerate(p):
            if "/" in f and f.endswith(".c"):
                loc = f.split("/")[-1] + ":" + (p[i + 1] if i + 1 < len(p) else "?")
            if f.startswith("count=") or f.startswith("index="):
                det = f
        rest = "|".join(x for x in p
                        if x.startswith(("confidence=", "impact=", "arr=",
                                         "count=", "index=", "kind=")))
        print(f"  {leaf:<12} {fn:<30} {loc:<22} {rest}")


if __name__ == "__main__":
    main()
