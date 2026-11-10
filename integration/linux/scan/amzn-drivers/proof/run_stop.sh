#!/bin/bash
# Queue-stop step of the induction: ena_tx_queue_release_bufs from any
# INV_STARTED && INV_FREE_LIST state.  Obligations plus vacuity witnesses.
set -u
RING=${RING:-8}
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/stop}
mkdir -p "$W"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
         ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
         ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get \
         __rte_raw_cksum ena_rss_configure eth_ena_pci_probe eth_ena_pci_remove \
         ena_interrupt_handler_rte ena_timer_wd_callback ena_populate_rx_queue \
         ena_rx_queue_release_bufs rte_mempool_cache_flush; do
  RM="$RM --remove-function-body $f"
done
FLAGS="--unwind $((RING + 2)) --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$B/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING_SIZE=$RING \
      -DVACUITY_CHECK=$V "$P/harness_stop.c" "$P/rdtsc_stub.c" -o "$W/s$V.gb" ) || exit 1
    [ -f "$W/s$V.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000; "$B/goto-instrument" $RM "$W/s$V.gb" "$W/s${V}r.gb" >/dev/null ) || exit 1
done
( ulimit -v 120000000; timeout 2400 "$B/cbmc" "$W/s0r.gb" --bounds-check \
  --pointer-check --unwinding-assertions $FLAGS ) > "$W/stop.log" 2>&1
( ulimit -v 120000000; timeout 2400 "$B/cbmc" "$W/s1r.gb" $FLAGS ) > "$W/stop_vac.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$W/stop.log" | grep -E "STOP_|FREE_ONCE|^VERIFICATION"
echo "other FAILURE: $(grep -E ': FAILURE$' "$W/stop.log" | grep -vc STOP_)"
# Every loop here has a constant bound (RING), so symex leaves them before the
# unwind limit and generates no unwinding assertion.  Show the bound is real:
# one unwinding short must produce a FAILING unwinding assertion.
( ulimit -v 32000000; timeout 900 "$B/cbmc" "$W/s0r.gb" --unwinding-assertions \
  --unwind "$RING" --sat-solver cadical --max-field-sensitivity-array-size 256 \
  --slice-formula ) 2>&1 | grep -qE "unwinding assertion.*FAILURE" &&
  echo "unwind check: --unwind $RING fails an unwinding assertion, --unwind $((RING + 2)) generates none (complete)"
echo "no-body FAILURE: $(grep -cE 'no body for callee.*FAILURE' "$W/stop.log")"
sed -E 's/\] line [0-9]+ /] /' "$W/stop_vac.log" | grep "vacuity witness"
