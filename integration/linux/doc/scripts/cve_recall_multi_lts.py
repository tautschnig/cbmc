#!/usr/bin/env python3
"""Targeted multi-LTS recall measurement on the 10 detected
CVEs from the methodology paper.

For each CVE, scan against EVERY LTS tree where the file
exists, in --invert mode (vulnerable direction).  Produces:

  - Per-tree recall table (rows scanned per tree, broken
    down by verdict).
  - Per-CVE × per-tree cross-tabulation.
  - Cleanly-testable count per tree.

Differs from cve_validate.py --multi-lts in that the CVE list
is HARD-CODED rather than randomly sampled.  This guarantees
the measurement covers every detected CVE, which a small
random sample (n=30) misses.

Usage:
  python3 cve_recall_multi_lts.py [--out-dir DIR]
"""
from __future__ import annotations

import argparse
import csv
import os
import sys
from pathlib import Path
from collections import Counter, defaultdict

ROOT = Path(__file__).resolve().parents[4]
SCRIPT_DIR = Path(__file__).resolve().parent
SCAN_DIR = ROOT / "integration" / "linux" / "scan"
sys.path.insert(0, str(SCRIPT_DIR))
sys.path.insert(0, str(SCAN_DIR))

import cve_validate  # type: ignore


# The 10 cleanly-testable CVEs the catalog detects, plus the
# (file_path, function, modules) tuple to scan each with.  These
# are pinned to the methodology-paper claims; they should be
# kept in sync with the "10 detected CVEs" list there.
#
# IMPORTANT: modules is a LIST — we try each in turn, taking
# the most-bug-finding verdict per CVE.  The methodology paper
# used --modules-per-cve 3; targeting the "right" module by
# auto-pick or by single hard-coded value loses CVEs whose
# detection depends on a fallback module.
DETECTED_CVES = [
    # (cve, category, file_path, function, [modules])
    ("CVE-2023-53038", "refcount_balance",
     None, None, ["refcount_lifetime", "lock_state",
                  "resource_leak_on_error_path"]),
    ("CVE-2023-53453", "resource_leak",
     None, None, ["resource_leak_on_error_path",
                  "use_after_free_generic", "module_lifetime"]),
    ("CVE-2023-53697", "resource_leak",
     None, None, ["resource_leak_on_error_path",
                  "use_after_free_generic"]),
    ("CVE-2024-35829", "resource_leak",
     None, None, ["resource_leak_on_error_path",
                  "null_after_alloc"]),
    ("CVE-2024-39492", "resource_leak",
     None, None, ["resource_leak_on_error_path",
                  "use_after_free_generic"]),
    ("CVE-2024-43818", "null_pointer_deref",
     "sound/soc/amd/acp-es8336.c",
     "st_es8336_late_probe",
     ["null_after_alloc"]),
    ("CVE-2025-21654", "refcount_balance",
     "fs/overlayfs/export.c",
     "ovl_connect_layer",
     ["dentry_lifetime"]),
    ("CVE-2025-21895", "resource_leak",
     None, None, ["resource_leak_on_error_path",
                  "lock_state", "use_after_free_generic"]),
    ("CVE-2025-40307", "refcount_balance",
     None, None, ["refcount_lifetime",
                  "resource_leak_on_error_path",
                  "use_after_free_generic"]),
    ("CVE-2026-43304", "resource_leak",
     None, None, ["resource_leak_on_error_path"]),
]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--kernel-trees", default=(
        "/home/ubuntu/linux_5_10,"
        "/home/ubuntu/linux_6_1,"
        "/home/ubuntu/linux_6_6,"
        "/home/ubuntu/linux_6_12"))
    ap.add_argument("--upstream-repo", type=Path,
                    default="/home/ubuntu/torvalds-linux.git")
    ap.add_argument("--sarif-out-dir", type=Path, default=None)
    args = ap.parse_args(argv)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    if args.sarif_out_dir:
        args.sarif_out_dir.mkdir(parents=True, exist_ok=True)
        os.environ["SARIF_OUTPUT_DIR"] = str(args.sarif_out_dir)

    kernel_trees = args.kernel_trees.split(",")

    # Resolve missing entries via _parse_patch.
    cases: list[cve_validate.CveCase] = []
    for cve, category, file_path, function, modules_list in DETECTED_CVES:
        if not file_path or not function:
            fp, fn, fh = cve_validate._parse_patch(cve)
            if not fp or not fn:
                print(f"  WARN: {cve} has no parseable patch; skipping",
                      file=sys.stderr)
                continue
            file_path = file_path or fp
            function = function or fn
        else:
            _, _, fh = cve_validate._parse_patch(cve)

        # Default to auto-pick if no modules specified.
        if not modules_list:
            modules_list = cve_validate._pick_modules(
                file_path, function,
                kernel_trees[0],
                max_modules=2)
            if not modules_list:
                print(f"  WARN: {cve} no module pick; skipping",
                      file=sys.stderr)
                continue

        # Find all LTS trees where the file exists.
        trees_with_file = cve_validate._find_files_in_trees(
            file_path, kernel_trees)
        if not trees_with_file:
            print(f"  WARN: {cve} file not in any LTS tree; "
                  f"skipping", file=sys.stderr)
            continue

        for tree in trees_with_file:
            for module in modules_list:
                cases.append(cve_validate.CveCase(
                    cve=cve, category=category,
                    summary="(detected-CVE recall test)",
                file_path=file_path, function=function,
                module=module, kernel_tree=tree,
                fix_hash=fh,
            ))

    print(f"Total cases (CVE × tree × module): {len(cases)}")
    upstream_repo = args.upstream_repo
    if upstream_repo and not (upstream_repo / "HEAD").exists():
        print(f"  warning: upstream-repo {upstream_repo} doesn't "
              f"look like a git dir; falling back to local-revert",
              file=sys.stderr)
        upstream_repo = None

    # Run scans.
    for i, c in enumerate(cases, 1):
        tree_short = Path(c.kernel_tree).name
        print(f"[{i}/{len(cases)}] {c.cve} {c.module} on "
              f"{tree_short}: {c.file_path}:{c.function} ...",
              flush=True)
        cve_validate._run_scan(
            c, timeout=args.timeout,
            invert=True,
            upstream_repo=upstream_repo,
        )
        print(f"    verdict={c.verdict}  {c.note[:100]}",
              flush=True)

    # Write CSV.
    csv_path = args.out_dir / "recall.csv"
    with open(csv_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["cve", "category", "module", "kernel_tree",
                    "file_path", "function", "verdict", "note"])
        for c in cases:
            w.writerow([c.cve, c.category, c.module or "",
                        c.kernel_tree or "",
                        c.file_path or "", c.function or "",
                        c.verdict, c.note])
    print(f"\nResults CSV: {csv_path}")

    # Per-tree breakdown.
    print("\n=== Per-tree breakdown ===")
    by_tree = defaultdict(Counter)
    for c in cases:
        tree = Path(c.kernel_tree).name
        by_tree[tree][c.verdict] += 1
    print(f"  {'tree':<14} {'cand':>5} {'fp':>5} {'succ':>5} "
          f"{'vac':>5} {'noise':>5} {'err':>5} {'skip':>5}")
    for tree in sorted(by_tree):
        d = by_tree[tree]
        print(f"  {tree:<14} {d.get('candidate',0):5d} "
              f"{d.get('fp-filtered',0):5d} "
              f"{d.get('successful',0):5d} "
              f"{d.get('vacuous',0):5d} "
              f"{d.get('noise',0):5d} "
              f"{d.get('error',0):5d} "
              f"{d.get('skipped',0)+d.get('timeout',0):5d}")

    # Per-CVE × per-tree cross-tab (best verdict across modules).
    print("\n=== Per-CVE × per-tree verdicts (best across modules) ===")
    BEST = ["candidate", "fp-filtered", "noise",
            "low-confidence-candidate",
            "successful", "vacuous", "timeout", "error",
            "skipped"]
    rank = {v: i for i, v in enumerate(BEST)}
    by_cve_tree: dict[tuple[str, str], str] = {}
    for c in cases:
        tree = Path(c.kernel_tree).name
        cur = by_cve_tree.get((c.cve, tree))
        if cur is None or rank.get(c.verdict, 99) < rank.get(cur, 99):
            by_cve_tree[(c.cve, tree)] = c.verdict
    cves_seen = sorted({c.cve for c in cases})
    trees_seen = sorted({Path(c.kernel_tree).name for c in cases})
    print(f"  {'CVE':<22} | "
          + " | ".join(f"{t[6:]:<10}" for t in trees_seen))
    for cve in cves_seen:
        row = [by_cve_tree.get((cve, t), "—") for t in trees_seen]
        print(f"  {cve:<22} | "
              + " | ".join(f"{v:<10}" for v in row))

    # Per-CVE BEST verdict (one detection per CVE wins).
    print("\n=== Per-CVE detection (any tree counts as detected) ===")
    per_cve_best: dict[str, str] = {}
    for c in cases:
        cur = per_cve_best.get(c.cve)
        if cur is None or rank.get(c.verdict, 99) < rank.get(cur, 99):
            per_cve_best[c.cve] = c.verdict
    counts = Counter(per_cve_best.values())
    print(f"  Total CVEs scanned: {len(per_cve_best)}")
    for v, n in sorted(counts.items(), key=lambda x: rank.get(x[0], 99)):
        cves = sorted(c for c, vv in per_cve_best.items() if vv == v)
        print(f"    {v}: {n} -- {', '.join(cves)}")

    detected_count = counts.get("candidate", 0)
    print(f"\n  Detected (≥1 tree fires CONTRACT VIOLATION): "
          f"{detected_count}/{len(per_cve_best)}")

    # Per-tree recall (cleanly-testable subset).
    print("\n=== Per-tree recall (cases where the LTS state "
          "was clean: already_vuln/reverted/upstream_vuln) ===")
    print(f"  {'tree':<14} {'detected':>9} {'fp-filt':>9} "
          f"{'tested':>9}")
    for tree in trees_seen:
        det = sum(1 for c in cases
                  if Path(c.kernel_tree).name == tree
                  and c.verdict == "candidate"
                  and "[state=" in (c.note or ""))
        fp = sum(1 for c in cases
                 if Path(c.kernel_tree).name == tree
                 and c.verdict == "fp-filtered"
                 and "[state=" in (c.note or ""))
        tested = sum(1 for c in cases
                     if Path(c.kernel_tree).name == tree
                     and "[state=" in (c.note or ""))
        print(f"  {tree:<14} {det:9d} {fp:9d} {tested:9d}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
