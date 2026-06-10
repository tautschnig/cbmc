#!/usr/bin/env python3
"""Kernel subsystem coverage census (data-driven).

Counts leaf subsystems per class (net/* fs/* drivers/* sound/* + top-level
atomic subsystems) in a kernel tree, then marks a subsystem COVERED if any
built CodeQL DB contains a compiled `.c` source under it (read from each
DB's src.zip).  Reports relative covered/uncovered, fully reproducibly from
the DB set.

Usage:
  subsystem_census.py [--tree /home/ubuntu/linux_6_12] [--dbs "/tmp/*-db"]
"""
import argparse
import glob
import os
import re
import zipfile

ATOMIC = ["crypto", "mm", "kernel", "security", "block", "ipc", "io_uring",
          "virt", "lib"]
PARENTS = ["net", "fs", "drivers", "sound"]


def leaf_subsystems(tree, parent):
    p = os.path.join(tree, parent)
    if not os.path.isdir(p):
        return set()
    return {parent + "/" + d for d in os.listdir(p)
            if os.path.isdir(os.path.join(p, d)) and not d.startswith(".")}


def covered_from_db(dbpath):
    """subsystem dirs with a compiled .c under them in this DB's src.zip."""
    z = os.path.join(dbpath, "src.zip")
    if not os.path.isfile(z):
        return set()
    cov = set()
    pat = re.compile(r"(?:^|/)((?:net|fs|drivers|sound)/[A-Za-z0-9_]+)/")
    atom = re.compile(r"(?:^|/)(" + "|".join(ATOMIC) + r")/[^/]*\.c$")
    try:
        with zipfile.ZipFile(z) as zf:
            for name in zf.namelist():
                if not name.endswith(".c"):
                    continue
                m = pat.search(name)
                if m:
                    cov.add(m.group(1))
                a = atom.search(name)
                if a:
                    cov.add(a.group(1))
    except zipfile.BadZipFile:
        pass
    return cov


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", default="/home/ubuntu/linux_6_12")
    ap.add_argument("--dbs", default="/tmp/*-db")
    a = ap.parse_args()

    classes = {p + "/*": leaf_subsystems(a.tree, p) for p in PARENTS}
    classes["top-level atomic"] = {d for d in ATOMIC
                                   if os.path.isdir(os.path.join(a.tree, d))}

    covered = set()
    dbs = sorted(glob.glob(a.dbs))
    for db in dbs:
        covered |= covered_from_db(db)

    print(f"# Kernel subsystem coverage census ({a.tree})")
    print(f"# {len(dbs)} CodeQL DBs scanned\n")
    print(f"{'class':<20}{'total':<8}{'covered':<9}{'pct':<7}")
    print("-" * 44)
    tot_all = cov_all = 0
    for cls, subs in classes.items():
        cov = subs & covered
        tot_all += len(subs)
        cov_all += len(cov)
        pct = 100.0 * len(cov) / len(subs) if subs else 0.0
        print(f"{cls:<20}{len(subs):<8}{len(cov):<9}{pct:>5.1f}%")
    print("-" * 44)
    print(f"{'TOTAL':<20}{tot_all:<8}{cov_all:<9}"
          f"{(100.0 * cov_all / tot_all if tot_all else 0):>5.1f}%")


if __name__ == "__main__":
    main()
