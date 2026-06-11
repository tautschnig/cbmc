#!/usr/bin/env python3
"""Kernel-wide candidate census across ALL subsystem classes (sound collector).

Generalises drivers_census to net/* fs/* sound/* drivers/* + the top-level
atomic subsystems, using the same per-leaf-DB unit model (heavy leaves
sub-scoped into depth-1 subdirs so the sound over-approximate collector
completes).  Reuses drivers_census.eval_unit/build_unit and the shared
/tmp/dcensus JSON cache (so the already-evaluated drivers units are reused).
Parallel + resumable.

Collector-architecture contract: this is the SOUND over-approximate
collector; advisories rank, never drop; only CBMC / sound checks / manual
review remove a candidate.

  full_census.py --build [--workers N]
  full_census.py --report
"""
import argparse
import multiprocessing as mp
import os

import drivers_census as dc
import outcome_summary as osum

TREE = dc.TREE
PARENTS = ["sound", "net", "fs", "drivers"]   # small-ish first
ATOMIC = ["crypto", "block", "ipc", "io_uring", "security", "virt",
          "mm", "lib", "kernel"]
# heavy leaves per class -> expand into depth-1 subdir units
HEAVY = {
    "net": {"wireless", "ethernet", "wwan", "dsa", "can", "phy"},
    "fs": {"btrfs", "xfs", "ext4", "f2fs", "cifs", "nfs", "nfsd", "ocfs2"},
    "drivers": dc.HEAVY,
    "sound": {"soc", "pci", "usb"},
}


def class_leaves(parent):
    p = os.path.join(TREE, parent)
    if not os.path.isdir(p):
        return []
    return sorted(d for d in os.listdir(p)
                  if os.path.isdir(os.path.join(p, d)) and not d.startswith("."))


def subdirs(parent, leaf):
    p = os.path.join(TREE, parent, leaf)
    return sorted(d for d in os.listdir(p)
                  if os.path.isdir(os.path.join(p, d))) if os.path.isdir(p) else []


def units():
    flat, heavy = [], []
    # drivers: reuse drivers_census units (same keys -> reuse cached JSONs)
    for u in dc.units():
        (heavy if "__" in u[2] else flat).append(u)
    for parent in ["sound", "net", "fs"]:
        for leaf in class_leaves(parent):
            hv = HEAVY.get(parent, set())
            if leaf in hv and subdirs(parent, leaf):
                for s in subdirs(parent, leaf):
                    heavy.append((f"{parent}/{leaf}/{s}/",
                                  f"/tmp/leaf-{parent}-{leaf}-{s}-db",
                                  f"{parent}__{leaf}__{s}"))
            else:
                flat.append((f"{parent}/{leaf}/",
                             f"/tmp/leaf-{parent}-{leaf}-db",
                             f"{parent}__{leaf}"))
    for d in ATOMIC:
        if os.path.isdir(os.path.join(TREE, d)):
            flat.append((f"{d}/", f"/tmp/leaf-{d}-db", f"atomic__{d}"))
    return flat + heavy


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--workers", type=int, default=8)
    a = ap.parse_args()
    os.makedirs(dc.OUT, exist_ok=True)
    us = units()
    if a.build:
        nfc = "/tmp/leaf-drivers-nfc-db"
        if os.path.isdir(nfc):
            osum.run_query(nfc, "tainted_count_into_fixed_array.ql", 60)
        with mp.Pool(a.workers) as p:
            for i, r in enumerate(p.imap_unordered(dc.eval_unit, us), 1):
                done = sum(v["status"] == "ok"
                           for v in r["assets"].values())
                print(f"[{i}/{len(us)}] {r['leaf']:<26} build={r['build']:<10} "
                      f"ok={done}/8 high={len(r.get('high', []))}", flush=True)
    if a.report or not a.build:
        dc.report(us)


if __name__ == "__main__":
    main()
