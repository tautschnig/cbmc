#!/bin/bash
# Summarise the collected metrics: where solver time goes, how many runs each
# artefact needed, and how the non-solver time divides up.
set -u
D=$(cd "$(dirname "$0")" && pwd)
echo "=== CBMC runs: totals ==="
awk -F, 'NR>1 {n++; w+=$4; c+=$5} END {
  if (n) printf "  %d runs, %.1f h wall, %.1f h cpu, %.1f min mean wall\n", n, w/3600, c/3600, (w/n)/60 }' "$D/cbmc-runs.csv" 2>/dev/null
echo
echo "=== CBMC runs: per artefact (runs, wall hours, verdicts) ==="
awk -F, 'NR>1 {n[$2]++; w[$2]+=$4; if ($7=="SUCCESSFUL") ok[$2]++; else if ($7=="NO_VERDICT"||$7=="OOM") nv[$2]++ }
  END { for (k in n) printf "  %-20s %3d runs  %6.2f h   %d successful, %d no-verdict/OOM\n", k, n[k], w[k]/3600, ok[k]+0, nv[k]+0 }' \
  "$D/cbmc-runs.csv" 2>/dev/null | sort -k2 -rn
echo
echo "=== where CBMC time goes (phase shares, runs that reported phases) ==="
awk -F, 'NR>1 && $13!="" {n++; sx+=$13; ss+=$14; so+=$15; t+=$13+$14+$15}
  END { if (n) printf "  %d runs: symex %.0f%%, SSA conversion %.0f%%, solver %.0f%%\n", n, 100*sx/t, 100*ss/t, 100*so/t }' \
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
        for (k in per) printf "    %-18s %.2f h\n", k, per[k]/3600 } }' "$D/tool-runs.csv"
echo
echo "=== activity journal: events and derived durations ==="
awk -F, 'NR>1 {print}' "$D/activity.csv" 2>/dev/null | sort | awk -F, '
  { if (prev_ts != "") { cmd = "date -d \"" $1 "\" +%s"; cmd | getline now; close(cmd);
      cmd2 = "date -d \"" prev_ts "\" +%s"; cmd2 | getline then; close(cmd2);
      d[prev_cat] += (now - then) }
    prev_ts=$1; prev_cat=$2 }
  END { for (k in d) printf "  %-10s %6.2f h\n", k, d[k]/3600 }' | sort -k2 -rn
echo
echo "=== iteration counts per artefact (from iterations.csv, hand-logged) ==="
[ -f "$D/iterations.csv" ] && column -s, -t "$D/iterations.csv" | sed 's/^/  /'
