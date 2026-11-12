#!/bin/bash
# Compare solver backends on identical goto-binaries.
#
# Solving is 98% of CBMC's time on these artefacts (see
# doc/proof-process-metrics-2026-11.md), so the backend choice is the obvious
# lever.  This runs each backend on the SAME .gb with the SAME flags and
# records wall time and verdict.  Verdicts must AGREE: a faster backend that
# disagrees is a bug to investigate, not a win.
set -u
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
CSV=${CSV:-$(cd "$(dirname "$0")" && pwd)/solver-bench.csv}
TO=${TO:-1800}
MODEL=${1:?model.gb}; LABEL=${2:?label}; shift 2
FLAGS=${*:---bounds-check --pointer-check --unwinding-assertions --unwind 12 --slice-formula}
[ -f "$CSV" ] || echo "label,backend,wall_s,verdict,failures,props" > "$CSV"
for cfg in "minisat2:--sat-solver minisat2" "cadical:--sat-solver cadical" \
           "glucose:--sat-solver glucose" "bitwuzla:--bitwuzla"; do
  name=${cfg%%:*}; opt=${cfg#*:}
  log=$(mktemp)
  start=$(date +%s.%N)
  ( ulimit -v 150000000; timeout "$TO" "$B/cbmc" "$MODEL" $opt $FLAGS ) > "$log" 2>&1
  wall=$(echo "$(date +%s.%N) - $start" | bc)
  v=$(grep -oE '^VERIFICATION [A-Z]+' "$log" | head -1 | sed 's/VERIFICATION //')
  if [ -z "$v" ]; then
    grep -qiE "unknown option|not supported|invalid" "$log" && v=UNSUPPORTED || v=NO_VERDICT
  fi
  f=$(grep -cE ': FAILURE$' "$log"); p=$(grep -cE ': (SUCCESS|FAILURE)$' "$log")
  printf '%-10s %-12s %7.1fs  %s (%s failing of %s)\n' "$LABEL" "$name" "$wall" "$v" "$f" "$p"
  printf '%s,%s,%.1f,%s,%s,%s\n' "$LABEL" "$name" "$wall" "$v" "$f" "$p" >> "$CSV"
  rm -f "$log"
done
