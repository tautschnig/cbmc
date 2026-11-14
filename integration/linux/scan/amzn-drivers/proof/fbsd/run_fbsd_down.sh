#!/bin/bash
# FreeBSD's ena_down against its TX datapath: the counterpart of
# harness_conc_datapath.c, and what makes F28's fix concrete.
#
# F28 is two defects in DPDK: the datapath's state check is compiled out
# (RTE_ETHDEV_DEBUG_TX), and ena_stop assigns STOPPED nineteen lines AFTER
# destroying the queues.  FreeBSD gets both halves right in the same driver
# family -- ena_mq_start tests IFF_DRV_RUNNING unconditionally
# (ena_datapath.c:149-150) and ena_down clears it BEFORE the teardown
# (ena.c:2634-2645) -- so this should PROVE the obligation DPDK fails.
#
#   asis         FreeBSD's ordering; both obligations must HOLD at every WHEN
#   broken-order -DBREAK_ORDER gives FreeBSD DPDK's ordering; they must FAIL
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
FB=${FB:-/tmp/fbsd-build}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/fbd}; mkdir -p "$OUT"
cd "$FB" || exit 1
U="--unwind 20 $SOLVER --slice-formula --unwinding-assertions"
for cfg in "asis:" "broken-order:-DBREAK_ORDER"; do
  n=${cfg%%:*}; d=${cfg#*:}
  for W in ${WHENS:-0 1 2}; do
    for V in 0 1; do
      vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
      rm -f "$OUT/$n$W$V.o" "$OUT/$n$W$V.gb"
      ( ulimit -v 32000000
        "$BIN/goto-cc" -c $(cat flags.txt) -DWHEN=$W $d $vd \
          "$HERE/harness_fbsd_down.c" -o "$OUT/$n$W$V.o" ) || exit 1
      [ -f "$OUT/$n$W$V.o" ] || { echo "BUILD FAILED"; exit 1; }
      ( ulimit -v 32000000
        "$BIN/goto-cc" "$OUT/$n$W$V.o" ena_com.o -o "$OUT/$n$W$V.gb" ) 2>/dev/null
    done
    echo "CBMC-CMD: cbmc $OUT/$n${W}0.gb --bounds-check --pointer-check $U" > "$OUT/$n$W.log"
    ( ulimit -v ${MEM:-48000000}; timeout ${TO:-1800} "$BIN/cbmc" "$OUT/$n${W}0.gb" \
        --bounds-check --pointer-check $U ) >> "$OUT/$n$W.log" 2>&1
    echo "CBMC-CMD: cbmc $OUT/$n${W}1.gb --no-standard-checks $U" > "$OUT/$n${W}_v.log"
    ( ulimit -v ${MEM:-48000000}; timeout ${TO:-1800} "$BIN/cbmc" "$OUT/$n${W}1.gb" \
        --no-standard-checks $U ) >> "$OUT/$n${W}_v.log" 2>&1
    printf '== %-13s WHEN=%s  ' "$n" "$W"
    grep -oE 'VERIFICATION [A-Z]+' "$OUT/$n$W.log" | head -1
    sed -E 's/\] line [0-9]+ /] /; s/^/     /' "$OUT/$n$W.log" | grep -E 'FBD_' | sort -u
    printf '     other FAILURE: %s   no-body: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
      "$(grep -E ': FAILURE$' "$OUT/$n$W.log" | grep -vcE 'FBD_|witness')" \
      "$(grep -cE 'no body for callee.*FAILURE' "$OUT/$n$W.log")" \
      "$(grep -cE ': UNKNOWN$' "$OUT/$n$W.log")" \
      "$(grep -cE 'unwind.*: FAILURE' "$OUT/$n$W.log")"
    sed -E 's/\] line [0-9]+ /] /; s/^/     /' "$OUT/$n${W}_v.log" | grep -E '\bwitness\b' | sort -u
  done
done
