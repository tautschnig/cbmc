#!/usr/bin/env python3
"""Kernel subsystem coverage census.

Counts leaf subsystems per class (net/* fs/* drivers/* sound/* + top-level
atomic subsystems) in a kernel tree, marks the ones the threat-model
pipeline has built a CodeQL DB for, and reports relative covered/uncovered.

Usage: subsystem_census.py [--tree /home/ubuntu/linux_6_12]
"""
import argparse
import os

# subsystems with a CodeQL DB built + run through threat_model_eval
COVERED = {
    # net/ (broad-next-db + rc7-db + nfc-db)
    "net/rxrpc", "net/ceph", "net/can", "net/netfilter", "net/bridge",
    "net/mac80211", "net/sctp", "net/ipv4", "net/mptcp", "net/6lowpan",
    "net/wireless", "net/nfc",
    # fs/
    "fs/ext4", "fs/hfsplus", "fs/f2fs", "fs/jfs", "fs/fat",
    # drivers/
    "drivers/hid",
    # sound/
    "sound/usb",
    # top-level atomic
    "crypto",
}

# top-level dirs that are themselves a single subsystem (not umbrellas)
ATOMIC = ["crypto", "mm", "kernel", "security", "block", "ipc", "io_uring",
          "virt", "lib"]


def leaf_subsystems(tree, parent):
    p = os.path.join(tree, parent)
    if not os.path.isdir(p):
        return []
    return sorted(parent + "/" + d for d in os.listdir(p)
                  if os.path.isdir(os.path.join(p, d)) and not d.startswith("."))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", default="/home/ubuntu/linux_6_12")
    a = ap.parse_args()

    classes = {}
    for parent in ["net", "fs", "drivers", "sound"]:
        classes[parent + "/*"] = leaf_subsystems(a.tree, parent)
    classes["top-level atomic"] = [d for d in ATOMIC
                                   if os.path.isdir(os.path.join(a.tree, d))]

    print(f"# Kernel subsystem coverage census ({a.tree})\n")
    print(f"{'class':<20}{'total':<8}{'covered':<9}{'pct':<7}")
    print("-" * 44)
    tot_all = cov_all = 0
    detail = {}
    for cls, subs in classes.items():
        cov = [s for s in subs if s in COVERED]
        tot_all += len(subs)
        cov_all += len(cov)
        pct = (100.0 * len(cov) / len(subs)) if subs else 0.0
        print(f"{cls:<20}{len(subs):<8}{len(cov):<9}{pct:>5.1f}%")
        detail[cls] = cov
    print("-" * 44)
    pct_all = 100.0 * cov_all / tot_all if tot_all else 0
    print(f"{'TOTAL':<20}{tot_all:<8}{cov_all:<9}{pct_all:>5.1f}%")

    print("\n## Covered subsystems\n")
    for cls, cov in detail.items():
        if cov:
            print(f"  {cls}: " + ", ".join(sorted(cov)))

    print("\n## Note")
    print("  Coverage is breadth-of-validation, not exhaustive scanning:")
    print("  all 5 major subsystem CLASSES (net/fs/drivers/sound/crypto) are")
    print("  exercised; the per-leaf fraction is small by design -- each DB")
    print("  build is on demand.  More DBs raise the count linearly.")


if __name__ == "__main__":
    main()
