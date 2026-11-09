#!/bin/bash
# RX teardown step: ena_rx_queue_release_bufs from an INV_RX_STARTED state.
set -u
RING=${RING:-8}
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/rx-stop}; mkdir -p "$W"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure ena_rss_hash_conf_get ena_rss_reta_query ena_rss_hash_update \
  ena_rss_reta_update eth_ena_pci_probe eth_ena_pci_remove \
  ena_interrupt_handler_rte ena_timer_wd_callback ena_populate_rx_queue \
  ena_tx_queue_release_bufs rte_mempool_cache_flush rte_mempool_put_bulk \
  rte_mempool_generic_put rte_mempool_do_generic_put; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind $((RING + 2)) --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$B/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING_SIZE=$RING \
      -DVACUITY_CHECK=$V "$P/harness_rx_stop.c" "$P/rdtsc_stub.c" \
      -o "$W/s$V.gb" 2>&1 | grep -i error | head ) || exit 1
  ( ulimit -v 32000000; "$B/goto-instrument" $RM "$W/s$V.gb" "$W/s${V}r.gb" >/dev/null 2>&1 )
done
( ulimit -v 150000000; timeout 3000 "$B/cbmc" "$W/s0r.gb" --bounds-check \
  --pointer-check --unwinding-assertions $FLAGS ) > "$W/rs.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$W/rs.log" | grep -E 'RXS_|^VERIFICATION'
echo "other FAILURE: $(grep -E ': FAILURE$' "$W/rs.log" | grep -vcE 'RXS_|rte_log_register|rte_pci_register')"
echo "no-body reached: $(grep -E 'no body for callee.*FAILURE' "$W/rs.log" | grep -vcE 'rte_log_register|rte_pci_register')"
echo "UNKNOWN: $(grep -c ': UNKNOWN$' "$W/rs.log")   ERROR: $(grep -c ': ERROR$' "$W/rs.log")"
( ulimit -v 150000000; timeout 3000 "$B/cbmc" "$W/s1r.gb" $FLAGS ) > "$W/rs_vac.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$W/rs_vac.log" | grep 'vacuity witness'
# the loop bound is RING; one short must fail an unwinding assertion
( ulimit -v 90000000; timeout 900 "$B/cbmc" "$W/s0r.gb" --unwinding-assertions \
  --unwind "$RING" --sat-solver cadical --max-field-sensitivity-array-size 256 \
  --slice-formula ) 2>&1 | grep -qE 'unwinding assertion.*FAILURE' &&
  echo "unwind check: one short fails an unwinding assertion (bound complete)"
