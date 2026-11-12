#!/bin/bash
# Summarise the collected metrics.
#
# A note on the activity split, because it is easy to misread.  Durations are
# derived from consecutive journal events, so an interval that contains a long
# background verification would charge that verification's wall time to
# whatever category happened to be logged before it.  The activity section
# therefore reports BOTH:
#   gross  the raw elapsed time between events
#   net    gross minus the CBMC wall time that overlaps the interval
# `net` is the figure to read as human effort; `gross` minus `net` is machine
# time that happened to run during it.
set -u
D=$(cd "$(dirname "$0")" && pwd)
echo "=== CBMC runs: totals ==="
awk -F, 'NR>1 {n++; w+=$4; c+=$5} END {
  if (n) printf "  %d runs, %.1f h wall, %.1f h cpu, %.1f min mean wall\n", n, w/3600, c/3600, (w/n)/60 }' "$D/cbmc-runs.csv" 2>/dev/null
echo
echo "=== CBMC runs: per artefact (runs, wall hours, verdicts) ==="
awk -F, 'NR>1 {n[$2]++; w[$2]+=$4; if ($7=="SUCCESSFUL") ok[$2]++; else if ($7=="NO_VERDICT"||$7=="OOM") nv[$2]++ }
  END { for (k in n) printf "  %-22s %3d runs  %6.2f h   %d successful, %d no-verdict/OOM\n", k, n[k], w[k]/3600, ok[k]+0, nv[k]+0 }' \
  "$D/cbmc-runs.csv" 2>/dev/null | sort -k2 -rn
echo
echo "=== where CBMC time goes (share of the phases it reports) ==="
awk -F, 'NR>1 && $13!="" {n++; sx+=$13; ss+=$14; so+=$15; t+=$13+$14+$15}
  END { if (n && t>0) printf "  %d runs: symex %.0f%%, SSA conversion %.0f%%, solver %.0f%%\n", n, 100*sx/t, 100*ss/t, 100*so/t }' \
  "$D/cbmc-runs.csv" 2>/dev/null
echo
echo "=== final-run vs iteration time ==="
awk -F, 'NR>1 { if ($3=="final") {fn++; fw+=$4} else {in_++; iw+=$4} }
  END { printf "  final runs:      %3d, %6.2f h\n  iteration runs:  %3d, %6.2f h\n", fn+0, fw/3600, in_+0, iw/3600 }' \
  "$D/cbmc-runs.csv" 2>/dev/null
echo
echo "=== other tools (goto-cc, goto-instrument) ==="
awk -F, 'NR>1 {n++; w+=$4; per[$2]+=$4}
  END { if (n>0) { printf "  %d invocations, %.2f h total\n", n, w/3600;
        for (k in per) printf "    %-18s %.2f h\n", k, per[k]/3600 } }' "$D/tool-runs.csv" 2>/dev/null
echo
echo "=== activity: gross elapsed vs net of concurrent CBMC time ==="
python3 - "$D" <<'PY'
import csv, sys, os, datetime
d = sys.argv[1]
def ts(s): return datetime.datetime.fromisoformat(s).timestamp()
try:
    acts = [(ts(r["ts"]), r["category"]) for r in csv.DictReader(open(f"{d}/activity.csv"))]
except Exception:
    sys.exit(0)
acts.sort()
runs = []
if os.path.exists(f"{d}/cbmc-runs.csv"):
    for r in csv.DictReader(open(f"{d}/cbmc-runs.csv")):
        try:
            end = ts(r["ts"]); runs.append((end - float(r["wall_s"]), end))
        except Exception:
            pass
gross, net = {}, {}
for (t0, cat), (t1, _) in zip(acts, acts[1:]):
    g = t1 - t0
    busy = sum(max(0.0, min(t1, e) - max(t0, s)) for s, e in runs)
    gross[cat] = gross.get(cat, 0.0) + g
    net[cat] = net.get(cat, 0.0) + max(0.0, g - busy)
if not gross:
    sys.exit(0)
for cat in sorted(gross, key=lambda c: -net[c]):
    print(f"  {cat:<10} gross {gross[cat]/3600:6.2f} h    net {net[cat]/3600:6.2f} h")
print(f"  {'TOTAL':<10} gross {sum(gross.values())/3600:6.2f} h    net {sum(net.values())/3600:6.2f} h")
print("  (the last event has no successor, so time after it is not counted)")
PY
echo
echo "=== iteration counts per artefact (hand-logged) ==="
[ -f "$D/iterations.csv" ] && awk -F, 'NR>1 {printf "  %-20s runs=%-5s inv_versions=%-3s harness_errors=%s\n", $1, $2, $3, $4}' "$D/iterations.csv"
