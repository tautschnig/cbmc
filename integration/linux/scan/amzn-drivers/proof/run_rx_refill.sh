#!/bin/bash
# RX refill sub-operation: the real ena_populate_rx_queue from an
# INV_RX_STARTED state, with the real ena_com_add_single_rx_desc.
set -u
RING=${RING:-4}
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/rx-refill}; mkdir -p "$W"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure ena_rss_hash_conf_get ena_rss_reta_query ena_rss_hash_update \
  ena_rss_reta_update eth_ena_pci_probe eth_ena_pci_remove \
  ena_interrupt_handler_rte ena_timer_wd_callback rte_mempool_cache_flush; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind $((RING + 3)) --unwindset ena_populate_rx_queue.0:$((RING + 1)),rte_pktmbuf_alloc_bulk.0:$((RING + 1)),rte_pktmbuf_free_bulk.0:$((RING + 1)) \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$B/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING_SIZE=$RING \
      -DVACUITY_CHECK=$V "$P/harness_rx_refill.c" "$P/rdtsc_stub.c" \
      -o "$W/f$V.gb" 2>&1 | grep -i error | head ) || exit 1
    [ -f "$W/f$V.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000; "$B/goto-instrument" $RM "$W/f$V.gb" "$W/f${V}r.gb" >/dev/null 2>&1 )
done
( ulimit -v 150000000; timeout 3000 "$B/cbmc" "$W/f0r.gb" --bounds-check \
  --unwinding-assertions $FLAGS ) > "$W/rf.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$W/rf.log" | grep -E 'RF_|^VERIFICATION'
echo "other FAILURE: $(grep -E ': FAILURE$' "$W/rf.log" | grep -vcE 'RF_|rte_log_register|rte_pci_register')"
echo "no-body reached: $(grep -E 'no body for callee.*FAILURE' "$W/rf.log" | grep -vcE 'rte_log_register|rte_pci_register')"
echo "unwinding FAILURE: $(grep -cE 'unwind.*: FAILURE$' "$W/rf.log")   UNKNOWN: $(grep -c ': UNKNOWN$' "$W/rf.log")"
( ulimit -v 150000000; timeout 3000 "$B/cbmc" "$W/f1r.gb" $FLAGS ) > "$W/rf_vac.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$W/rf_vac.log" | grep 'vacuity witness'
