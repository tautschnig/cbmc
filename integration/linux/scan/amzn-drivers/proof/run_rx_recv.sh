#!/bin/bash
# RX data-path step: eth_ena_recv_pkts from an INV_RX_STARTED state, untrusted
# device completions.
set -u
RING=${RING:-4}; NB=${NB:-1}; MC=${MC:-1}
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/rx}; mkdir -p "$W"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts ena_xmit_mbuf ena_tx_map_mbuf \
  ena_tx_cleanup ena_xstats_get ena_xstats_get_names ena_xstats_get_by_id \
  ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum ena_rss_configure \
  ena_rss_hash_conf_get ena_rss_reta_query ena_rss_hash_update ena_rss_reta_update \
  eth_ena_pci_probe eth_ena_pci_remove ena_interrupt_handler_rte \
  ena_timer_wd_callback ena_populate_rx_queue rte_mempool_cache_flush \
  rte_pktmbuf_alloc_bulk rte_mempool_get_bulk; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind $((RING + 2)) --unwindset eth_ena_recv_pkts.0:$((NB + 1)),ena_rx_mbuf.0:$((MC + 1)),ena_com_cdesc_rx_pkt_get.0:$((MC + 2)) \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$B/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING_SIZE=$RING \
      -DNB_PKTS=$NB -DMAXCHAIN=$MC -DASSUME_NO_F16 -DVACUITY_CHECK=$V "$P/harness_rx_recv.c" "$P/rdtsc_stub.c" \
      -o "$W/r$V.gb" 2>&1 | grep -i error | head ) || exit 1
  ( ulimit -v 32000000; "$B/goto-instrument" $RM "$W/r$V.gb" "$W/r${V}r.gb" >/dev/null 2>&1 )
done
( ulimit -v 150000000; timeout 3600 "$B/cbmc" "$W/r0r.gb" --bounds-check \
  --pointer-check --unwinding-assertions $FLAGS ) > "$W/rx.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$W/rx.log" | grep -E 'RX_|^VERIFICATION'
echo "other FAILURE: $(grep -E ': FAILURE$' "$W/rx.log" | grep -vcE 'RX_|rte_log_register|rte_pci_register')"
echo "no-body reached: $(grep -E 'no body for callee.*FAILURE' "$W/rx.log" | grep -vcE 'rte_log_register|rte_pci_register')"
echo "unwinding FAILURE: $(grep -cE 'unwind.*: FAILURE$' "$W/rx.log")   UNKNOWN: $(grep -c ': UNKNOWN$' "$W/rx.log")"
( ulimit -v 150000000; timeout 3600 "$B/cbmc" "$W/r1r.gb" $FLAGS ) > "$W/rx_vac.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$W/rx_vac.log" | grep 'vacuity witness'
# recv's loops are bounded by the device completion count; one unwinding short
# must fail an unwinding assertion, so the bound above is complete
( ulimit -v 90000000; timeout 900 "$B/cbmc" "$W/r0r.gb" --unwinding-assertions \
  --unwind 3 --unwindset eth_ena_recv_pkts.0:1,ena_rx_mbuf.0:2,ena_com_cdesc_rx_pkt_get.0:3 \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula ) 2>&1 |
  grep -qE 'unwinding assertion.*FAILURE' &&
  echo "unwind check: one short fails an unwinding assertion (bound complete)"
