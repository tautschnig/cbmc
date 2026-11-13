#!/bin/bash
# FreeBSD ENA ena_rx_mbuf: the RX assembly step.  Carries the two F16 checks
# DPDK fails -- an unposted slot and a req_id repeated inside one chain -- and
# the DMA-ownership obligation DPDK cannot express.
#
# An out-of-RANGE req_id is deliberately not modelled: shared ena_com rejects
# it in ena_com_rx_pkt (ena-com/ena_eth_com.c:638-640), so it is unreachable
# here and there is no RX counterpart to F11.
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
FB=${FB:-/tmp/fbsd-build}
HERE=$(cd "$(dirname "$0")" && pwd)
RING=${RING:-4}
CHAIN=${CHAIN:-2}
OUT=${OUT:-/tmp/ena-proof/fbsd-rx}; mkdir -p "$OUT"
cd "$FB" || exit 1
RM=""
for f in ena_rss_configure ena_rss_key_fill ena_qflush ena_deferred_mq_start \
  ena_cleanup ena_tx_cleanup ena_start_xmit ena_mq_start ena_handle_msix \
  ena_xmit_mbuf ena_tx_map_mbuf ena_rx_cleanup ena_refill_rx_bufs; do
  RM="$RM --remove-function-body $f"; done
for V in 0 1; do
  vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
  rm -f "$OUT/h$V.o" "$OUT/g$V.gb" "$OUT/r$V.gb"
  ( ulimit -v 32000000
    "$BIN/goto-cc" -c $(cat flags.txt) -DRING_SIZE=$RING -DMAXCHAIN=$CHAIN $vd \
      "$HERE/harness_fbsd_rx.c" -o "$OUT/h$V.o" ) || exit 1
  [ -f "$OUT/h$V.o" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000
    "$BIN/goto-cc" "$OUT/h$V.o" ena.o ena_com.o -o "$OUT/g$V.gb" ) 2>/dev/null
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/g$V.gb" "$OUT/m$V.gb" >/dev/null 2>&1 )
  ( ulimit -v 32000000; "$BIN/goto-instrument" --add-library "$OUT/m$V.gb" "$OUT/r$V.gb" >/dev/null 2>&1 )
done
U="--unwind $((RING + CHAIN + 4)) --unwindset ena_rx_mbuf.0:$((CHAIN + 1)),memcmp.0:9"
echo "CBMC-CMD: cbmc $OUT/r0.gb --bounds-check --pointer-check --unwinding-assertions $U $SOLVER --slice-formula" > "$OUT/x.log"
( ulimit -v ${MEM:-120000000}; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/r0.gb" --bounds-check \
  --pointer-check --unwinding-assertions $U $SOLVER --slice-formula ) >> "$OUT/x.log" 2>&1
sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/x.log" | grep -E 'FBR_|^   VERIFICATION' | sort -u
printf '   other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
  "$(grep -E ': FAILURE$' "$OUT/x.log" | grep -vcE 'FBR_|witness')" \
  "$(grep -cE 'no body for callee.*FAILURE' "$OUT/x.log")" \
  "$(grep -cE ': UNKNOWN$' "$OUT/x.log")" \
  "$(grep -cE 'unwind.*: FAILURE' "$OUT/x.log")"
echo "CBMC-CMD: cbmc $OUT/r1.gb --no-standard-checks --unwinding-assertions $U $SOLVER --slice-formula" > "$OUT/v.log"
( ulimit -v ${MEM:-120000000}; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/r1.gb" --no-standard-checks \
  --unwinding-assertions $U $SOLVER --slice-formula ) >> "$OUT/v.log" 2>&1
sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/v.log" | grep -E '\bwitness\b' | sort -u
