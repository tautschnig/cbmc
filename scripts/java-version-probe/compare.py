#!/usr/bin/env python3
"""Compare the outputs of Probe on several JDKs.

For each method, lists the cases whose results differ between versions,
with the versions that give each result (as ranges). A case where some
version ran out of memory, or where the method is absent, is compared
among the other versions only.

Usage: compare.py OUT_DIR [--examples N] [--json FILE]
OUT_DIR holds VERSION.txt files written by Probe.
"""

import argparse
import collections
import json
import re
from pathlib import Path


def ranges(versions):
    versions = sorted(versions)
    out, start, prev = [], None, None
    for v in versions:
        if start is None:
            start = prev = v
        elif v == prev + 1 or (prev == 8 and v == 9):
            prev = v
        else:
            out.append((start, prev))
            start = prev = v
    if start is not None:
        out.append((start, prev))
    return ",".join(str(a) if a == b else f"{a}-{b}" for a, b in out)


def load(path):
    cases = {}
    for line in path.open(encoding="utf-8", errors="replace"):
        line = line.rstrip("\n").split("\t", 1)[0]
        parts = line.split("|", 2)
        if len(parts) == 2:
            cases[(parts[0], None)] = parts[1]
        elif len(parts) == 3 and parts[1].isdigit():
            cases[(parts[0], int(parts[1]))] = parts[2]
    return cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("--examples", type=int, default=3)
    ap.add_argument("--json")
    ap.add_argument("--agreeing", type=int, default=0,
                    help="also write up to N cases per method where all "
                    "versions agree (for checking JBMC on them)")
    args = ap.parse_args()
    data = {}
    for f in Path(args.out_dir).glob("*.txt"):
        if re.fullmatch(r"\d+", f.stem):
            data[int(f.stem)] = load(f)
    versions = sorted(data)
    keys = sorted({k for d in data.values() for k in d if k[1] is not None},
                  key=lambda k: (k[0], k[1]))
    by_method = collections.OrderedDict()
    agreeing = collections.OrderedDict()
    for key in keys:
        results = {}
        for v in versions:
            r = data[v].get(key)
            if r is None or r.endswith("=> OOM"):
                continue
            results[v] = r
        if len(set(results.values())) == 1:
            same = agreeing.setdefault(key[0], [])
            if len(same) < args.agreeing:
                same.append((key[1], {next(iter(results.values())):
                                      ranges(results)}))
            continue
        if len(set(results.values())) == 0:
            continue
        by_result = collections.defaultdict(list)
        for v, r in results.items():
            by_result[r].append(v)
        by_method.setdefault(key[0], []).append(
            (key[1], {r: ranges(vs) for r, vs in by_result.items()}))
    report = []
    for method, diffs in by_method.items():
        # the partitions of the versions, most frequent first
        parts = collections.Counter(
            " | ".join(sorted(p.values())) for _, p in diffs)
        print(f"{method}: {len(diffs)} cases differ; partitions: "
              + "; ".join(f"{p} ({n})" for p, n in parts.most_common()))
        for k, p in diffs[: args.examples]:
            for r, vs in sorted(p.items(), key=lambda x: x[1]):
                print(f"    [{k}] {vs}: {r[:300]}")
        report.append({"method": method, "cases": [
            {"case": k, "results": p} for k, p in diffs]})
    if args.json:
        for method, cases in agreeing.items():
            report.append({"method": method, "agreeing": True, "cases": [
                {"case": k, "results": p} for k, p in cases]})
        Path(args.json).write_text(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
