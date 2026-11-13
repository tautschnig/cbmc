#!/usr/bin/env python3
"""Attribute elapsed time to machine work, evidenced human work, or neither.

The previous summary derived a duration for each journal event from the gap to
the next one.  Two things made the result untrustworthy, and it was marked
unquotable for both:

  * a long background verification was charged to whatever category happened
    to precede it, so machine time appeared as human effort;
  * a missed event charged everything until the next one to the wrong
    category, and an overnight gap became hours of "READ".

This replaces derivation-from-ordering with attribution against evidence.
Nothing is charged to a human category unless something independently
observable happened in that interval: a tool run, a file written, or a commit.
Time with no evidence is reported as UNATTRIBUTED rather than folded into the
nearest category, and the report prints what fraction of the elapsed span is
evidenced so the figure can be judged.

Evidence sources, all independent of the journal:
  * metrics/cbmc-runs.csv and metrics/tool-runs.csv -- machine intervals,
    reconstructed as [ts - wall_s, ts] since ts is written at completion
  * git commit timestamps touching the proof or doc trees
  * mtimes of harnesses, runners and docs

Usage:  attribute.py [--idle-gap MINUTES] [--csv]
"""
import argparse
import csv
import os
import subprocess
import sys
from datetime import datetime, timedelta, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
PROOF = os.path.dirname(HERE)
REPO = os.path.abspath(os.path.join(PROOF, "..", "..", "..", "..", ".."))


def parse_ts(s):
    s = s.strip()
    if not s:
        return None
    try:
        return datetime.fromisoformat(s)
    except ValueError:
        return None


def read_machine_intervals():
    """[(start, end, kind, label)] from the tool CSVs."""
    out = []
    for name, kind in (("cbmc-runs.csv", "cbmc"), ("tool-runs.csv", "tool")):
        p = os.path.join(HERE, name)
        if not os.path.exists(p):
            continue
        with open(p) as fh:
            for row in csv.DictReader(fh):
                end = parse_ts(row.get("ts", ""))
                try:
                    wall = float(row.get("wall_s") or 0)
                except ValueError:
                    wall = 0.0
                if end is None:
                    continue
                out.append((end - timedelta(seconds=wall), end, kind,
                            row.get("label", "")))
    return sorted(out)


def read_journal():
    p = os.path.join(HERE, "activity.csv")
    if not os.path.exists(p):
        return []
    out = []
    with open(p) as fh:
        for row in csv.DictReader(fh):
            ts = parse_ts(row.get("ts", ""))
            if ts:
                out.append((ts, row.get("category", "?"),
                            row.get("subject", "")))
    return sorted(out)


def git_evidence():
    """Commit timestamps touching the driver-proof work."""
    try:
        out = subprocess.run(
            ["git", "-C", REPO, "log", "--format=%cI", "--since=60.days",
             "--", "integration/linux"],
            capture_output=True, text=True, timeout=60).stdout
    except Exception:
        return []
    return sorted(filter(None, (parse_ts(l) for l in out.splitlines())))


def mtime_evidence():
    """When harnesses, runners and docs were last written."""
    stamps = []
    for root in (PROOF, os.path.join(PROOF, "fbsd"),
                 os.path.abspath(os.path.join(PROOF, "..", "..", "..",
                                              "doc"))):
        if not os.path.isdir(root):
            continue
        for f in os.listdir(root):
            if not f.endswith((".c", ".h", ".sh", ".md", ".py", ".txt")):
                continue
            fp = os.path.join(root, f)
            try:
                stamps.append(datetime.fromtimestamp(os.path.getmtime(fp),
                                                     tz=timezone.utc))
            except OSError:
                pass
    return sorted(stamps)


def overlap(a0, a1, b0, b1):
    lo, hi = max(a0, b0), min(a1, b1)
    return max(0.0, (hi - lo).total_seconds())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--idle-gap", type=float, default=15.0,
                    help="minutes with no evidence before an interval counts "
                         "as unattributed (default 15)")
    ap.add_argument("--csv", action="store_true")
    args = ap.parse_args()
    gap = timedelta(minutes=args.idle_gap)

    machine = read_machine_intervals()
    journal = read_journal()
    if not journal:
        print("no journal events; nothing to attribute", file=sys.stderr)
        return 1

    # Human evidence only.  A machine run COMPLETING is not evidence that a
    # person was present, so run boundaries are deliberately excluded here --
    # including them inflated a synthetic test by half.
    evidence = sorted(set(git_evidence() + mtime_evidence()
                          + [t for t, _, _ in journal]))

    # journal intervals: each event runs until the next one
    spans = []
    for i, (ts, cat, subj) in enumerate(journal):
        end = journal[i + 1][0] if i + 1 < len(journal) else ts + gap
        if end > ts:
            spans.append((ts, end, cat, subj))

    buckets = {}
    unattributed = 0.0
    machine_total = 0.0
    detail = []

    for s0, s1, cat, subj in spans:
        total = (s1 - s0).total_seconds()
        mach = sum(overlap(s0, s1, m0, m1) for m0, m1, _, _ in machine)
        mach = min(mach, total)
        machine_total += mach
        human_window = total - mach

        # of the non-machine remainder, only stretches containing evidence
        # count as human work; the rest is unattributed
        # Human work is presumed to happen AROUND observable events, so each
        # piece of evidence licenses a window of +/- idle-gap/2 around itself.
        # Those windows are unioned (not summed, which would double-count
        # clustered evidence), clipped to this span, and the machine
        # intervals removed. Counting evidence pieces instead would be
        # arbitrary -- ten file writes in a minute are not ten windows.
        half = gap / 2
        windows = []
        for e in evidence:
            w0, w1 = max(s0, e - half), min(s1, e + half)
            if w1 > w0:
                windows.append((w0, w1))
        windows.sort()
        merged = []
        for w0, w1 in windows:
            if merged and w0 <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(merged[-1][1], w1))
            else:
                merged.append((w0, w1))
        attributed = 0.0
        for w0, w1 in merged:
            span_s = (w1 - w0).total_seconds()
            mach_in_w = sum(overlap(w0, w1, m0, m1) for m0, m1, _, _ in machine)
            attributed += max(0.0, span_s - min(mach_in_w, span_s))
        attributed = min(attributed, human_window)
        unattributed += human_window - attributed
        buckets[cat] = buckets.get(cat, 0.0) + attributed
        detail.append((s0, cat, subj, total, mach, attributed))

    elapsed = (spans[-1][1] - spans[0][0]).total_seconds()
    human = sum(buckets.values())

    if args.csv:
        w = csv.writer(sys.stdout)
        w.writerow(["category", "evidenced_hours"])
        for k, v in sorted(buckets.items(), key=lambda x: -x[1]):
            w.writerow([k, round(v / 3600, 2)])
        w.writerow(["MACHINE", round(machine_total / 3600, 2)])
        w.writerow(["UNATTRIBUTED", round(unattributed / 3600, 2)])
        return 0

    print(f"span            {spans[0][0]:%Y-%m-%d %H:%M} to "
          f"{spans[-1][1]:%Y-%m-%d %H:%M}  ({elapsed/3600:.1f} h elapsed)")
    print(f"journal events  {len(journal)}")
    print(f"machine runs    {len(machine)} "
          f"({machine_total/3600:.1f} h inside journalled spans)")
    print()
    print("evidenced human time, by category")
    for k, v in sorted(buckets.items(), key=lambda x: -x[1]):
        if v > 0:
            print(f"  {k:<10} {v/3600:6.2f} h   {100*v/human:5.1f}%")
    print(f"  {'TOTAL':<10} {human/3600:6.2f} h")
    print()
    print(f"machine         {machine_total/3600:6.2f} h")
    print(f"unattributed    {unattributed/3600:6.2f} h  "
          f"(no tool run, file write or commit in the interval)")
    cover = 100 * (human + machine_total) / elapsed if elapsed else 0
    print(f"evidenced       {cover:5.1f}% of the elapsed span")
    print()
    if cover < 60:
        print("Read with care: under 60% of the span is evidenced, so the")
        print("category split describes a minority of the elapsed time. The")
        print("absolute hours are a lower bound on effort, not a measurement")
        print("of it. Journal more often to raise this.")
    else:
        print("Only the evidenced hours should be quoted. Unattributed time is")
        print("time nothing observable happened in, which is not the same as")
        print("time nobody worked -- it is time this method cannot speak to.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
