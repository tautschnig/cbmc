#!/usr/bin/env python3
"""Kernel-wide skb/cursor (field-read-before-length-check) triage.

Runs skb_field_before_lencheck.ql over every cached per-leaf CodeQL DB,
collects all candidates, and classifies each:

  HEADER-HELPER    -- defined in an include/ header: a generic skb accessor
                      (skb_copy_to_linear_data, skb_header_pointer, ...) whose
                      length check is the CALLER's responsibility (analogous
                      to count/index bareparam) -- not a subsystem parser.
  LOCALLY-GUARDED  -- adv_lenguard=GUARDED (a pskb_may_pull / skb->len check
                      in the function) or adv_trivial=yes (*_hdr accessor).
  FRONTIER         -- UNGUARDED, non-trivial, in a subsystem .c: the genuine
                      read-before-length-check OOB-read candidates (the length
                      guard, if any, is non-local -> caller_precondition's
                      skb-pull interprocedural verdict settles these).

Parallel + resumable.  Recall for this shape is validated in
ground_truth_recall.sh (read_field_at_offset / decode_le16_at_cursor).

  wide_triage_skb.py --run [--workers N]
  wide_triage_skb.py --report
"""
import argparse
import glob
import json
import multiprocessing as mp
import os

import triage_loop as tl

OUT = "/tmp/wskb"


def dbs():
    return sorted(d for d in glob.glob("/tmp/leaf-*-db")
                  if os.path.isfile(d + "/src.zip"))


def run_db(db):
    jp = f"{OUT}/{os.path.basename(db)}.json"
    if os.path.isfile(jp):
        return json.load(open(jp))
    try:
        lines = tl.run_oracle(db, "skb_field_before_lencheck.ql")
    except Exception:
        lines = None
    res = {"db": os.path.basename(db), "ok": lines is not None,
           "lines": lines or []}
    json.dump(res, open(jp, "w"))
    return res


def classify(line):
    p = line.split("|")
    path = p[1] if len(p) > 1 else ""
    if "/include/" in path:
        return "HEADER-HELPER"
    if "adv_lenguard=GUARDED" in line or "adv_trivial=yes" in line \
       or "adv_lenprop=LENPROP" in line:
        return "LOCALLY-GUARDED"
    return "FRONTIER"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--workers", type=int, default=10)
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
    kind = Counter()
    n_ok = total = 0
    seen = set()
    frontier = []
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
            sig = (p[0], p[1].split("/")[-1] if len(p) > 1 else "",
                   p[2] if len(p) > 2 else "")
            if sig in seen:
                continue
            seen.add(sig)
            total += 1
            c = classify(l)
            cls[c] += 1
            kind[p[3] if len(p) > 3 else "?"] += 1
            if c == "FRONTIER":
                frontier.append((sig[0], sig[1], sig[2],
                                 p[3] if len(p) > 3 else ""))
    print(f"\n# kernel-wide skb/cursor triage ({n_ok}/{len(ds)} leaf DBs, "
          f"{total} distinct candidates)\n")
    print("## triage class")
    for k, c in cls.most_common():
        print(f"  {k:<18}{c}  ({100*c//max(total,1)}%)")
    print("\n## kind")
    for k, c in kind.most_common():
        print(f"  {k:<30}{c}")
    print(f"\n## FRONTIER (subsystem-.c, UNGUARDED, non-trivial): "
          f"{len(frontier)} -- caller-precondition skb-pull settles these")
    from collections import Counter as C
    byfile = C(f for _, f, _, _ in frontier)
    for fl, c in byfile.most_common(20):
        print(f"  {c:>3}  {fl}")


if __name__ == "__main__":
    main()
