#!/usr/bin/env python3
"""Detect when driver source a harness cites has changed underneath it.

Why this exists: the cleanup depth results (ring 8, 16, 32) do not call
ena_tx_cleanup.  They prove an invariant over a hand TRANSCRIPTION of the
driver's loop body, quoted in harness_cleanup_step.c from
ena_ethdev.c:3229-3253.  Nothing would notice if the driver's loop changed and
the transcription stopped matching it, so the proof could quietly come to be
about code that no longer exists.

The same risk applies, less acutely, to every `file.c:N-M` citation in a
harness: each one is a claim about what the driver does at that location, and
line ranges rot.  This fingerprints the cited region instead of trying to diff
prose against code.

What it does NOT do, stated plainly: it cannot check that a transcription is
FAITHFUL, only that the source it was taken from has not moved.  Faithfulness
is a human reading.  What this buys is that the reading is re-triggered when,
and only when, the source changes.

  check_citations.py              check every citation against the manifest
  check_citations.py --update     record current fingerprints (review the diff)

Exit 0 if every cited region matches, 1 if any drifted or cannot be read.
"""
import argparse
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "source-citations.txt")

# Where each cited basename lives.  DPDK sources are in a scratch tree, so a
# missing root is reported rather than silently skipped.
ROOTS = {
    "dpdk": "/tmp/dpdk/drivers/net/ena",
    "dpdk_base": "/tmp/dpdk/drivers/net/ena/base",
    "fbsd": "/home/ubuntu/amzn-drivers/kernel/fbsd/ena",
    "fbsd_com": "/home/ubuntu/amzn-drivers/kernel/fbsd/ena/ena-com",
}
CITATION = re.compile(r"\b([a-z_0-9]+\.c):(\d+)-(\d+)\b")


def resolve(basename, from_fbsd):
    """Candidate paths for a cited basename, FreeBSD first if cited there."""
    order = (["fbsd", "fbsd_com", "dpdk", "dpdk_base"] if from_fbsd
             else ["dpdk", "dpdk_base", "fbsd", "fbsd_com"])
    for key in order:
        p = os.path.join(ROOTS[key], basename)
        if os.path.isfile(p):
            return p
    return None


def normalise(lines):
    """Strip what is not semantics: indentation, blank lines, comment-only
    lines, and runs of internal whitespace.  Reformatting the driver should
    not trip the check; changing what it does should."""
    out = []
    for ln in lines:
        s = ln.strip()
        if not s:
            continue
        if s.startswith("/*") or s.startswith("*") or s.startswith("//"):
            continue
        out.append(re.sub(r"\s+", " ", s))
    return "\n".join(out)


def fingerprint(path, lo, hi):
    with open(path, errors="replace") as fh:
        lines = fh.read().splitlines()
    if hi > len(lines):
        return None, f"cites lines {lo}-{hi} but the file has {len(lines)}"
    region = lines[lo - 1:hi]
    body = normalise(region)
    if not body:
        return None, f"lines {lo}-{hi} normalise to nothing"
    return hashlib.sha256(body.encode()).hexdigest()[:16], None


def collect():
    """[(harness, basename, lo, hi)] for every citation in every harness."""
    found = []
    for d in (HERE, os.path.join(HERE, "fbsd")):
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not (f.startswith("harness_") and f.endswith(".c")):
                continue
            p = os.path.join(d, f)
            text = open(p, errors="replace").read()
            for m in CITATION.finditer(text):
                base, lo, hi = m.group(1), int(m.group(2)), int(m.group(3))
                if lo > hi:
                    continue
                found.append((os.path.relpath(p, HERE), base, lo, hi))
    # de-duplicate: the same citation often appears more than once
    return sorted(set(found))


def load_manifest():
    d = {}
    if not os.path.exists(MANIFEST):
        return d
    for ln in open(MANIFEST):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        parts = ln.split()
        if len(parts) == 2:
            d[parts[0]] = parts[1]
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()

    citations = collect()
    manifest = load_manifest()
    rows, drifted, unreadable, new = [], [], [], []

    for harness, base, lo, hi in citations:
        key = f"{harness}:{base}:{lo}-{hi}"
        path = resolve(base, harness.startswith("fbsd/"))
        if path is None:
            unreadable.append((key, f"no source tree holds {base}"))
            continue
        fp, err = fingerprint(path, lo, hi)
        if fp is None:
            unreadable.append((key, err))
            continue
        rows.append((key, fp))
        if key not in manifest:
            new.append(key)
        elif manifest[key] != fp:
            drifted.append((key, manifest[key], fp))

    if args.update:
        with open(MANIFEST, "w") as fh:
            fh.write("# Fingerprints of the driver source each harness cites.\n")
            fh.write("# Generated by check_citations.py --update.  A changed\n")
            fh.write("# fingerprint means the cited region moved or changed, so\n")
            fh.write("# the harness's claim about it needs re-reading -- most\n")
            fh.write("# urgently harness_cleanup_step.c, which TRANSCRIBES its\n")
            fh.write("# citation rather than calling the code.\n")
            for key, fp in sorted(rows):
                fh.write(f"{key} {fp}\n")
        print(f"recorded {len(rows)} fingerprints in {MANIFEST}")
        if unreadable:
            print(f"warning: {len(unreadable)} citation(s) unreadable",
                  file=sys.stderr)
            for key, why in unreadable:
                print(f"  {key}: {why}", file=sys.stderr)
        return 0

    print(f"{len(rows)} citations checked, {len(manifest)} in the manifest")
    rc = 0
    if drifted:
        rc = 1
        print("\nDRIFTED -- the cited source changed, so the harness's claim "
              "about it must be re-read:")
        for key, was, now in drifted:
            print(f"  {key}\n      manifest {was}  now {now}")
        if any("cleanup_step" in k for k, _, _ in drifted):
            print("\n  harness_cleanup_step.c TRANSCRIBES its citation rather "
                  "than calling it.\n  Its ring 8/16/32 results are only as "
                  "good as that transcription, so\n  re-check the loop body "
                  "line by line before trusting them again.")
    if unreadable:
        rc = 1
        print("\nUNREADABLE -- cannot verify, which is not the same as passing:")
        for key, why in unreadable:
            print(f"  {key}: {why}")
    if new:
        rc = 1
        print(f"\nNOT IN THE MANIFEST ({len(new)}): run --update after "
              f"checking each is cited correctly:")
        for key in new[:10]:
            print(f"  {key}")
    if rc == 0:
        print("every cited region matches its recorded fingerprint")
    return rc


if __name__ == "__main__":
    sys.exit(main())
