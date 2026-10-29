#!/usr/bin/env python3
"""Compare per-file node-kind counts from the native C++ Go parser
(`goparse --counts`) with those from go/parser (`go_oracle`).

Usage: compare_counts.py NATIVE_COUNTS ORACLE_COUNTS

Each input line is "path kind=count kind=count ...". Exits non-zero if
any file differs, or if a file is missing from either side, and prints
the first example of each mismatching kind.
"""
import collections
import sys


def load(path):
    counts = {}
    with open(path) as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "FAIL":  # native parser: "FAIL path:l:c: msg"
                counts[parts[1].split(":")[0]] = {"FAIL": None}
                continue
            if parts[1:] == ["ERROR"]:  # go/parser rejected the file
                counts[parts[0]] = {"ERROR": None}
                continue
            counts[parts[0]] = dict(p.split("=") for p in parts[1:])
    return counts


def main(native_path, oracle_path):
    native, oracle = load(native_path), load(oracle_path)
    mismatches = collections.defaultdict(list)
    missing = sorted(set(native) ^ set(oracle))
    for path in sorted(set(native) & set(oracle)):
        if oracle[path] == {"ERROR": None}:
            continue
        if native[path] == {"FAIL": None}:
            mismatches["parse-failure"].append((path, "FAIL", "ok"))
            continue
        kinds = set(native[path]) | set(oracle[path])
        for kind in kinds:
            a = int(native[path].get(kind, 0))
            b = int(oracle[path].get(kind, 0))
            if a != b:
                mismatches[kind].append((path, a, b))
    bad_files = {p for v in mismatches.values() for p, _, _ in v}
    print(f"files compared: {len(set(native) & set(oracle))}; "
          f"files with mismatches: {len(bad_files)}; "
          f"files missing on one side: {len(missing)}")
    for kind, items in sorted(mismatches.items(), key=lambda kv: -len(kv[1])):
        path, a, b = items[0]
        print(f"  {kind}: {len(items)} files, e.g. {path} native={a} "
              f"go/parser={b}")
    for path in missing[:10]:
        print(f"  missing: {path}")
    return 1 if bad_files or missing else 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
