#!/bin/bash
# The second half of the base case: ena_queue_start establishes INV_STARTED.
# Together with the setup base case (run_stage3_basecase.sh) and the TX step
# (run_item1_realbodies_unbounded.sh) this closes the induction, under the
# callee models for ena_com_create_io_queue / ena_com_get_io_handlers stated in
# harness_start_basecase.c.
set -eu
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof}
RING=${RING:-8}
mkdir -p "$OUT"
[ -f "$OUT/flags.txt" ] || { echo "run run.sh first (it writes flags.txt)"; exit 1; }
cd "$DPDK/build-ql"

# Off the queue-start path.  The datapath and statistics entry points come in
# through CBMC's function-pointer expansion of dev_ops; the RX side comes in
# through the expansion at rte_mempool_ops_dequeue_bulk although the harness
# starts a TX ring.  Any of them actually reached would be reported as
# "no body for callee", which the runner checks below.
RM=""
for f in ena_xstats_get ena_xstats_get_names ena_xstats_get_by_id \
         ena_xstats_get_names_by_id ena_stats_get eth_ena_prep_pkts \
         eth_ena_xmit_pkts eth_ena_recv_pkts ena_tx_map_mbuf ena_xmit_mbuf \
         check_for_tx_completion_in_queue __rte_raw_cksum ena_rss_configure \
         eth_ena_pci_probe eth_ena_pci_remove ena_interrupt_handler_rte \
         ena_timer_wd_callback ena_populate_rx_queue rte_pktmbuf_alloc_bulk \
         rte_mempool_do_generic_get rte_mempool_ops_dequeue_bulk \
         rte_mempool_get_bulk ena_com_add_single_rx_desc; do
  RM="$RM --remove-function-body $f"
done

# --max-field-sensitivity-array-size 256: struct ena_adapter holds
# tx_ring[128] and ena_com_dev holds io_sq_queues[256].  Above CBMC's default
# limit of 64 elements the fields of tx_ring[0] are not tracked individually,
# a pointer read back from them has `unknown` in its points-to set, and the
# free-list initialisation in ena_create_io_queue becomes a write to anything.
# See cbmc-limitations/field_sensitivity_array_limit.sh.
CHECKS="--unwind $((RING + 4)) --sat-solver cadical --max-field-sensitivity-array-size 256"

for V in 0 1; do
  ( ulimit -v 32000000
    "$BIN/goto-cc" $(cat "$OUT/flags.txt") -DPEDANTIC -DCONCRETE_RING=$RING \
      -DASSUME_DEVICE_HONEST_DOORBELL -DVACUITY_CHECK=$V \
      "$HERE/harness_start_basecase.c" -o "$OUT/s4_$V.gb" )
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/s4_$V.gb" "$OUT/s4_${V}_rm.gb" >/dev/null 2>&1 )
done

rm -f "$OUT/stage4.log" "$OUT/stage4_vac.log"
( ulimit -v 150000000
  timeout 1800 "$BIN/cbmc" "$OUT/s4_0_rm.gb" --bounds-check --pointer-check \
    $CHECKS --unwinding-assertions > "$OUT/stage4.log" 2>&1 ) || true
if ! grep -qE '^VERIFICATION' "$OUT/stage4.log"; then
  echo "NO VERDICT: cbmc did not finish"; tail -3 "$OUT/stage4.log"; exit 1
fi
grep -E '^VERIFICATION|^\*\* [0-9]' "$OUT/stage4.log"
grep -E 'base case|error path' "$OUT/stage4.log" | sed 's/^/  /'
echo "unmodelled callees actually reached:"
grep -E 'no body for callee.*FAILURE|undefined function.*FAILURE' "$OUT/stage4.log" | sed 's/^/  /' || echo "  none"

( ulimit -v 150000000
  timeout 1800 "$BIN/cbmc" "$OUT/s4_1_rm.gb" $CHECKS > "$OUT/stage4_vac.log" 2>&1 ) || true
echo "vacuity witnesses (both must be FAILURE):"
grep -E 'vacuity witness' "$OUT/stage4_vac.log" | sed 's/^/  /'
