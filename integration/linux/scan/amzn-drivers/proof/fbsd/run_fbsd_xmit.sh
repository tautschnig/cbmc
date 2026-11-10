#!/bin/bash
# FreeBSD ENA ena_xmit_mbuf: the TX submit step, with the F12 check
# (FBX_NO_UNSENT) that DPDK fails and FreeBSD should pass, and the
# DMA-ownership obligation DPDK cannot express.
set -u
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
FB=${FB:-/tmp/fbsd-build}
HERE=$(cd "$(dirname "$0")" && pwd)
RING=${RING:-4}
OUT=${OUT:-/tmp/ena-proof/fbsd-xmit}; mkdir -p "$OUT"
cd "$FB" || exit 1
# functions off the submit path, or kernel services with no body; removing
# them keeps the reached-bodyless-callee count at zero on the path that
# matters, and any that IS reached shows up as a no-body FAILURE below
RM=""
for f in ena_rss_configure ena_rss_key_fill ena_qflush ena_deferred_mq_start \
  ena_cleanup ena_rx_cleanup ena_tx_cleanup ena_rx_mbuf ena_refill_rx_bufs \
  ena_start_xmit ena_mq_start ena_handle_msix; do
  RM="$RM --remove-function-body $f"; done
for V in 0 1; do
  ( ulimit -v 32000000
    "$BIN/goto-cc" -c $(cat flags.txt) -DRING_SIZE=$RING -DVACUITY_CHECK=$V \
      -DASSUME_NO_F14 "$HERE/harness_fbsd_xmit.c" -o "$OUT/h$V.o" ) || exit 1
    [ -f "$OUT/h$V.o" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000
    "$BIN/goto-cc" "$OUT/h$V.o" ena.o ena_com.o -o "$OUT/g$V.gb" ) 2>/dev/null
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/g$V.gb" "$OUT/m$V.gb" >/dev/null 2>&1 )
  # memcpy and friends come from CBMC's model library, not the FreeBSD build
  ( ulimit -v 32000000; "$BIN/goto-instrument" --add-library \
      --replace-calls __builtin_memcpy:fbsd_memcpy_model \
      "$OUT/m$V.gb" "$OUT/r$V.gb" >/dev/null 2>&1 )
done
U="--unwind $((RING + 6)) --unwindset ena_tx_map_mbuf.0:$((RING + 2)),bus_dmamap_load_mbuf_sg.0:4,memcmp.0:9,fbsd_memcpy_model.0:98"
( ulimit -v 200000000; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/r0.gb" --bounds-check \
  --pointer-check --unwinding-assertions $U --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula ) > "$OUT/x.log" 2>&1
grep -qE '^VERIFICATION' "$OUT/x.log" || { echo "NO VERDICT"; tail -3 "$OUT/x.log"; }
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E 'FBX_|^VERIFICATION'
echo "other FAILURE: $(grep -E ': FAILURE$' "$OUT/x.log" | grep -vcE 'FBX_')"
echo "no-body reached: $(grep -cE 'no body for callee.*FAILURE' "$OUT/x.log")"
echo "UNKNOWN: $(grep -c ': UNKNOWN$' "$OUT/x.log")   ERROR: $(grep -c ': ERROR$' "$OUT/x.log")   unwind FAIL: $(grep -cE 'unwind.*: FAILURE$' "$OUT/x.log")"
( ulimit -v 200000000; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/r1.gb" $U \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula ) > "$OUT/x_vac.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/x_vac.log" | grep 'vacuity witness'
