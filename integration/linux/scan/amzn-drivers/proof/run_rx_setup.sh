#!/bin/bash
# RX base case: ena_rx_queue_setup establishes INV_RX_STARTABLE, including
# under allocation failure.
set -u
RING=${RING:-4}
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/rx-setup}; mkdir -p "$W"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure ena_rss_hash_conf_get ena_rss_reta_query ena_rss_hash_update \
  ena_rss_reta_update eth_ena_pci_probe eth_ena_pci_remove \
  ena_interrupt_handler_rte ena_timer_wd_callback ena_populate_rx_queue \
  rte_mempool_cache_flush; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind $((RING + 3)) --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$B/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DCONCRETE_RING=$RING \
      -DVACUITY_CHECK=$V "$P/harness_rx_setup.c" "$P/rdtsc_stub.c" \
      -o "$W/u$V.gb" 2>&1 | grep -i error | head ) || exit 1
    [ -f "$W/u$V.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000; "$B/goto-instrument" $RM "$W/u$V.gb" "$W/u${V}r.gb" >/dev/null 2>&1 )
done
( ulimit -v 150000000; timeout 3000 "$B/cbmc" "$W/u0r.gb" --bounds-check \
  --pointer-check --unwinding-assertions $FLAGS ) > "$W/ru.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$W/ru.log" | grep -E 'base case|RXSU_|error path|^VERIFICATION'
echo "other FAILURE: $(grep -E ': FAILURE$' "$W/ru.log" | grep -vcE 'base case|RXSU_|error path|rte_log_register|rte_pci_register')"
echo "no-body reached: $(grep -E 'no body for callee.*FAILURE' "$W/ru.log" | grep -vcE 'rte_log_register|rte_pci_register')"
echo "UNKNOWN: $(grep -c ': UNKNOWN$' "$W/ru.log")   ERROR: $(grep -c ': ERROR$' "$W/ru.log")"
( ulimit -v 150000000; timeout 3000 "$B/cbmc" "$W/u1r.gb" $FLAGS ) > "$W/ru_vac.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$W/ru_vac.log" | grep 'vacuity witness'
