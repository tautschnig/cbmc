#!/bin/bash
# Device lifecycle: ena_start over every queue, with the error-path obligation.
set -u
RING=${RING:-4}; NQ=${NQ:-1}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/devstart}; mkdir -p "$OUT"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure ena_rss_hash_conf_get ena_rss_reta_query ena_rss_hash_update \
  ena_rss_reta_update eth_ena_pci_probe eth_ena_pci_remove \
  ena_interrupt_handler_rte ena_timer_wd_callback; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind $((RING + 3)) --unwindset ena_populate_rx_queue.0:$((RING + 1)),rte_pktmbuf_alloc_bulk.0:$((NQ * RING + 1)),ena_create_io_queue.0:$((RING + 1)),ena_create_io_queue.1:$((RING + 1)),ena_queue_start_all.0:$((NQ + 1)),ena_queue_start_all.1:$((NQ + 1)),ena_queue_stop_all.0:$((NQ + 1)) \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  D="${DEFS:-}"; [ $V = 1 ] && D="$D -DVACUITY_CHECK"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING=$RING -DNQ=$NQ \
      $D "$HERE/harness_dev_start.c" "$HERE/rdtsc_stub.c" -o "$OUT/s$V.gb" \
      2>&1 | grep -i error | head ) || exit 1
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/s$V.gb" "$OUT/r$V.gb" >/dev/null 2>&1 )
done
( ulimit -v 150000000; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/r0.gb" --bounds-check \
  --unwinding-assertions $FLAGS ) > "$OUT/x.log" 2>&1
grep -qE '^VERIFICATION' "$OUT/x.log" || { echo "NO VERDICT"; tail -3 "$OUT/x.log"; }
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E 'DS_|^VERIFICATION' | sort -u
echo "other FAILURE: $(grep -E ': FAILURE$' "$OUT/x.log" | grep -vcE 'DS_')"
echo "no-body reached: $(grep -cE 'no body for callee.*FAILURE' "$OUT/x.log")"
echo "UNKNOWN: $(grep -c ': UNKNOWN$' "$OUT/x.log")   ERROR: $(grep -c ': ERROR$' "$OUT/x.log")   unwind FAIL: $(grep -cE 'unwind.*: FAILURE$' "$OUT/x.log")"
( ulimit -v 150000000; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/r1.gb" $FLAGS ) > "$OUT/v.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/v.log" | grep 'vacuity witness'
