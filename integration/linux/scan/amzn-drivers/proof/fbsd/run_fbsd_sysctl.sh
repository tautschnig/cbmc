#!/bin/bash
# Class S: FreeBSD's stats_sample_interval sysctl reports success for a write
# it rejected.  Confirms the Velocity Labs class Z finding "Rejected
# stats_sample_interval writes report success to sysctl(8) and scripts".
#
# ena_sysctl_metrics_interval computes `error` on three rejection paths and
# then returns 0 unconditionally (ena_sysctl.c:886-931).
#
#   as-is   SS_RC_SOUND and SS_NO_SILENT must FAIL
#   fixed   -DWITH_RC_FIX models `return (error)`; both must hold
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
FB=${FB:-/tmp/fbsd-build}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/fbsd-sysctl}; mkdir -p "$OUT"
cd "$FB" || exit 1
U="--unwind 20 $SOLVER --slice-formula --unwinding-assertions"
for cfg in "as-is:" "fixed:-DWITH_RC_FIX"; do
  n=${cfg%%:*}; d=${cfg#*:}
  for V in 0 1; do
    vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
    rm -f "$OUT/$n$V.o" "$OUT/$n$V.gb"
    ( ulimit -v 32000000
      "$BIN/goto-cc" -c $(cat flags.txt) $d $vd \
        "$HERE/harness_fbsd_sysctl.c" -o "$OUT/$n$V.o" ) || exit 1
    [ -f "$OUT/$n$V.o" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
    ( ulimit -v 32000000
      "$BIN/goto-cc" "$OUT/$n$V.o" ena_com.o -o "$OUT/$n$V.gb" ) 2>/dev/null
  done
  echo "CBMC-CMD: cbmc $OUT/${n}0.gb --bounds-check --pointer-check $U" > "$OUT/$n.log"
  ( ulimit -v ${MEM:-60000000}; timeout ${TO:-1800} "$BIN/cbmc" "$OUT/${n}0.gb" \
      --bounds-check --pointer-check $U ) >> "$OUT/$n.log" 2>&1
  echo "CBMC-CMD: cbmc $OUT/${n}1.gb --no-standard-checks $U" > "$OUT/${n}_v.log"
  ( ulimit -v ${MEM:-60000000}; timeout ${TO:-1800} "$BIN/cbmc" "$OUT/${n}1.gb" \
      --no-standard-checks $U ) >> "$OUT/${n}_v.log" 2>&1
  echo "== $n"
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/$n.log" | grep -E 'SS_|^   VERIFICATION' | sort -u
  printf '   other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
    "$(grep -E ': FAILURE$' "$OUT/$n.log" | grep -vcE 'SS_|witness')" \
    "$(grep -cE 'no body for callee.*FAILURE' "$OUT/$n.log")" \
    "$(grep -cE ': UNKNOWN$' "$OUT/$n.log")" \
    "$(grep -cE 'unwind.*: FAILURE' "$OUT/$n.log")"
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/${n}_v.log" | grep -E '\bwitness\b' | sort -u
done
