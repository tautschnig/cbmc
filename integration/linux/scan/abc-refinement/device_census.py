#!/usr/bin/env python3
"""Kernel-wide device-as-attacker census (device_tainted_sink.ql).

Same per-leaf-DB unit model as drivers_census/full_census, but scoped to
subsystems where a DMA-capable device exists, and running the device-taint
finder instead of the user/wire-input ones.  Parallel, resumable (one JSON
per unit under /tmp/devcensus).

Collector-architecture contract: the query is the SOUND over-approximate
collector; the advisories rank and never drop.  Only CBMC, a sound check or
manual review removes a candidate.

  device_census.py --build [--workers N] [--classes a,b,c]
  device_census.py --report
"""
import argparse
import glob
import json
import multiprocessing as mp
import os
import subprocess

import drivers_census as dc
import outcome_summary as osum

TREE = dc.TREE
SCAN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = "/tmp/devcensus"
QUERY = "device_tainted_sink.ql"
Q_TIMEOUT = 900
BUILD_TIMEOUT = 3600

# Subsystem classes ordered by how much device-controlled DMA they carry.
# Heavy ones are expanded into depth-1 subdir units so extraction completes.
CLASSES = [
    "drivers/net/ethernet", "drivers/infiniband", "drivers/nvme",
    "drivers/net/wireless", "drivers/scsi", "drivers/vdpa", "drivers/virtio",
    "drivers/vfio", "drivers/block", "drivers/crypto", "drivers/dma",
    "drivers/mmc", "drivers/usb", "drivers/ata", "drivers/net/can",
    "drivers/net/wwan", "drivers/firewire", "drivers/thunderbolt",
    "drivers/accel", "drivers/media/pci", "drivers/media/usb",
    "drivers/gpu/drm", "drivers/misc", "drivers/char",
]
EXPAND = {
    "drivers/net/ethernet", "drivers/infiniband", "drivers/scsi",
    "drivers/net/wireless", "drivers/usb", "drivers/gpu/drm",
    "drivers/media/pci", "drivers/media/usb", "drivers/misc",
}


def subdirs(rel):
    p = os.path.join(TREE, rel)
    if not os.path.isdir(p):
        return []
    return sorted(d for d in os.listdir(p)
                  if os.path.isdir(os.path.join(p, d)) and not d.startswith("."))


def units(classes):
    """(target, db, key) per unit, small classes first so results arrive early."""
    out = []
    for rel in classes:
        if not os.path.isdir(os.path.join(TREE, rel)):
            continue
        key0 = rel.replace("/", "-")
        if rel in EXPAND and subdirs(rel):
            for s in subdirs(rel):
                out.append((f"{rel}/{s}/", f"/tmp/leaf-{key0}-{s}-db",
                            f"{key0}__{s}"))
        else:
            out.append((f"{rel}/", f"/tmp/leaf-{key0}-db", key0))
    return out


def build_unit(target, db):
    if os.path.isfile(db + "/src.zip"):
        return "cached"
    # percpu.h is pre-patched once by main() for the whole parallel run
    cmd = ["bash", "-c",
           f"ulimit -v 96000000; timeout {BUILD_TIMEOUT} "
           f"env PERCPU_PREPATCHED=1 {SCAN}/build-codeql-db.sh "
           f"{TREE} {db} {target} 8 >/dev/null 2>&1"]
    subprocess.run(cmd)
    return "built" if os.path.isfile(db + "/src.zip") else "build-fail"


def eval_unit(unit):
    target, db, key = unit
    jpath = f"{OUT}/{key}.json"
    if os.path.isfile(jpath):
        return json.load(open(jpath))
    res = {"leaf": key, "target": target, "build": build_unit(target, db),
           "status": "no-db", "lines": []}
    if os.path.isfile(db + "/src.zip"):
        status, lines = osum.run_query(db, QUERY, Q_TIMEOUT)
        res["status"] = status
        res["lines"] = lines
    json.dump(res, open(jpath, "w"))
    return res


def field(line, key):
    for f in line.split("|"):
        if f.startswith(key + "="):
            return f.split("=", 1)[1]
    return ""


def priority(line):
    """A candidate is high-priority when nothing locally bounds it and the
    sink can corrupt memory (fixed-size index or a copy length)."""
    kind = line.split("|")[3] if len(line.split("|")) > 3 else ""
    unbounded = (field(line, "adv_guard") == "no" and
                 field(line, "adv_mask") == "no" and
                 field(line, "adv_typebound") == "no")
    return unbounded and kind in ("index-fixed", "copy-len")


PERCPU = f"{TREE}/arch/x86/include/asm/percpu.h"
GATE = "#ifdef CONFIG_CC_HAS_NAMED_AS"
OFF = "#if 0 /* codeql: disable __seg_gs */"


def percpu_patch(on):
    """Apply/revert the __seg_gs extractor workaround once for the whole run."""
    s = open(PERCPU).read()
    s = s.replace(GATE, OFF) if on else s.replace(OFF, GATE)
    open(PERCPU, "w").write(s)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--classes", default="")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    classes = a.classes.split(",") if a.classes else CLASSES
    us = units(classes)

    if a.build:
        print(f"## device census: {len(us)} units, {a.workers} workers")
        percpu_patch(True)
        try:
          with mp.Pool(a.workers) as pool:
            for i, r in enumerate(pool.imap_unordered(eval_unit, us), 1):
                hi = sum(1 for l in r["lines"] if priority(l))
                print(f"[{i}/{len(us)}] {r['leaf']:<34} build={r['build']:<10} "
                      f"{r['status']:<8} rows={len(r['lines']):<5} prio={hi}",
                      flush=True)
        finally:
            percpu_patch(False)

    if a.report:
        res = [json.load(open(p)) for p in sorted(glob.glob(f"{OUT}/*.json"))]
        ok = [r for r in res if r["status"] == "ok"]
        rows = [(r["leaf"], l) for r in ok for l in r["lines"]]
        prio = [(k, l) for k, l in rows if priority(l)]
        print(f"\n## device-taint census: {len(res)} units attempted, "
              f"{len(ok)} with results")
        for st in ("no-db", "timeout", "error"):
            n = sum(1 for r in res if r["status"] == st)
            if n:
                print(f"   {st}: {n} units")
        print(f"   {len(rows)} sinks, {len(prio)} priority "
              f"(unguarded fixed-index / copy-length)\n")
        by_kind = {}
        for _, l in rows:
            k = l.split("|")[3]
            by_kind[k] = by_kind.get(k, 0) + 1
        for k, n in sorted(by_kind.items(), key=lambda x: -x[1]):
            print(f"   {k:<14}{n}")
        print("\n## priority candidates")
        seen = set()
        for leaf, l in sorted(prio):
            p = l.split("|")
            sig = (p[0], p[2], p[3])
            if sig in seen:
                continue
            seen.add(sig)
            print(f"  {leaf:<28}{p[0]:<34}{os.path.basename(p[1])}:{p[2]:<6}"
                  f"{p[3]:<12}{field(l, 'src')}")


if __name__ == "__main__":
    main()
