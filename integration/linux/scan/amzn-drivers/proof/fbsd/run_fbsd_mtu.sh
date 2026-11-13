#!/bin/bash
# Class S: FreeBSD's SIOCSIFMTU reports success for an MTU the device rejected.
#
# ena_change_mtu is careful (ena.c:331-352); its caller discards the result
# (ena.c:2426) and returns ena_up's instead.  Confirms the Unveil class Z
# finding "MTU change reported as successful although the device rejected it".
#
#   as-is   SM_RC_SOUND and SM_NO_SILENT must FAIL
#   fixed   -DWITH_RC_FIX propagates the result; both must hold
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
FB=${FB:-/tmp/fbsd-build}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/fbsd-mtu}; mkdir -p "$OUT"
cd "$FB" || exit 1
U="--unwind 20 $SOLVER --slice-formula --unwinding-assertions"
for cfg in "as-is:" "fixed:-DWITH_RC_FIX"; do
  n=${cfg%%:*}; d=${cfg#*:}
  for V in 0 1; do
    vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
    rm -f "$OUT/$n$V.o" "$OUT/$n$V.gb"
    ( ulimit -v 32000000
      "$BIN/goto-cc" -c $(cat flags.txt) $d $vd \
        "$HERE/harness_fbsd_mtu.c" -o "$OUT/$n$V.o" ) || exit 1
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
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/$n.log" | grep -E 'SM_|^   VERIFICATION' | sort -u
  printf '   other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
    "$(grep -E ': FAILURE$' "$OUT/$n.log" | grep -vcE 'SM_|witness')" \
    "$(grep -cE 'no body for callee.*FAILURE' "$OUT/$n.log")" \
    "$(grep -cE ': UNKNOWN$' "$OUT/$n.log")" \
    "$(grep -cE 'unwind.*: FAILURE' "$OUT/$n.log")"
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/${n}_v.log" | grep -E '\bwitness\b' | sort -u
done
