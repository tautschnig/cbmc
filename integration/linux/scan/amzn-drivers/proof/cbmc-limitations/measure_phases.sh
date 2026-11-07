#!/bin/bash
# Run one cbmc configuration and report where the time goes.
#
#   measure_phases.sh TAG MODEL.gb TIMEOUT_S [cbmc flags...]
#
# Prints one line per phase that was reached, with its monotonic timestamp,
# plus the SSA size, VCC counts, propositional size and the verdict.  A phase
# that is missing from the output is the phase the run died in, which is the
# point: "timeout" on its own says nothing about where the cost is.
set -u
TAG=$1; MODEL=$2; TO=$3; shift 3
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
MEM=${MEM:-150000000}            # KiB of address space, i.e. ~143 GiB
LOG=${LOGDIR:-/tmp/phases}/$TAG.log
mkdir -p "$(dirname "$LOG")"

start=$(date +%s.%N)
( ulimit -v "$MEM"
  timeout "$TO" "$BIN/cbmc" "$MODEL" "$@" --verbosity 9 \
    --timestamp monotonic > "$LOG" 2>&1 )
rc=$?
wall=$(echo "$(date +%s.%N) - $start" | bc)

case $rc in
  0) verdict=SUCCESSFUL ;; 10) verdict=FAILED ;; 124) verdict=TIMEOUT ;;
  6) verdict=OUT-OF-MEMORY ;; *) verdict="rc=$rc" ;;
esac

rt() { grep -oE "Runtime $1: [0-9.]+s" "$LOG" | tail -1 | grep -oE '[0-9.]+s$'; }
steps=$(grep -oE 'size of program expression: [0-9]+' "$LOG" | tail -1 | grep -oE '[0-9]+$')
vccs=$(grep -oE 'Generated [0-9]+ VCC\(s\), [0-9]+ remaining' "$LOG" | tail -1 | sed -E 's/Generated //; s/ remaining//')
prop=$(grep -oE '[0-9]+ variables, [0-9]+ clauses' "$LOG" | tail -1)
# CBMC always runs a "simple" slicer; --slice-formula adds a second, stronger
# one.  Report them separately so the two are not confused.
simple=$(grep -oE 'simple slicing removed [0-9]+' "$LOG" | tail -1 | grep -oE '[0-9]+$')
formula=$(grep -E 'slicing removed [0-9]+ assignments' "$LOG" | grep -v simple | tail -1 | grep -oE 'removed [0-9]+' | grep -oE '[0-9]+')

printf '%-28s %-14s wall=%7.1fs\n' "$TAG" "$verdict" "$wall"
printf '    symex           %-10s %s steps\n' "$(rt Symex)" "${steps:-?}"
printf '    slicing         simple=%s  slice-formula=%s assignments removed\n' "${simple:-0}" "${formula:-off}"
printf '    VCCs            %s\n' "${vccs:-not reached}"
printf '    convert SSA     %-10s %s\n' "$(rt 'Convert SSA')" "${prop:-not reached}"
printf '    solver          %-10s\n' "$(rt Solver)"
printf '    last line       %s\n' "$(tail -1 "$LOG" | cut -c16-100)"
