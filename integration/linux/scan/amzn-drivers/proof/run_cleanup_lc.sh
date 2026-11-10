#!/bin/bash
# The cleanup step under a LOOP CONTRACT instead of bounded unwinding, so the
# cost no longer grows with the ring size.  Target: ring 8, which did not
# finish under unwinding (8.8M clauses, no verdict in 40 min).
set -u
RING=${RING:-8}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/cleanup-lc}; mkdir -p "$OUT"
sed "s/RING_SIZE_INV/$RING/g" "$HERE/cleanup_loop_contract.json.in" \
  > "$OUT/contract.json"
DEFS=${DEFS:-}
RM=""
for f in eth_ena_prep_pkts eth_ena_recv_pkts eth_ena_xmit_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_xstats_get ena_xstats_get_names ena_xstats_get_by_id \
  ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum ena_rss_configure \
  eth_ena_pci_probe eth_ena_pci_remove ena_interrupt_handler_rte \
  ena_timer_wd_callback ena_populate_rx_queue; do
  RM="$RM --remove-function-body $f"; done
for V in 0 1; do
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING_SIZE=$RING \
      -DMBUF_POOL=$RING -DVACUITY_CHECK=$V $DEFS "$HERE/harness_cleanup.c" \
      "$HERE/rdtsc_stub.c" -o "$OUT/c$V.gb" 2>&1 | grep -i error | head ) || exit 1
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/c$V.gb" "$OUT/c${V}_rm.gb" >/dev/null 2>&1 )
  ( ulimit -v 32000000; "$BIN/goto-instrument" --add-library "$OUT/c${V}_rm.gb" "$OUT/c${V}_lib.gb" >/dev/null 2>&1 )
  ( ulimit -v 32000000; "$BIN/goto-instrument" --no-malloc-may-fail \
      --loop-contracts-file "$OUT/contract.json" \
      --dfcc main --apply-loop-contracts "$OUT/c${V}_lib.gb" "$OUT/c${V}_lc.gb" ) \
      > "$OUT/inst$V.log" 2>&1 || { echo "instrumentation failed:"; tail -5 "$OUT/inst$V.log"; exit 1; }
done
( ulimit -v 150000000; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/c0_lc.gb" \
  --bounds-check --pointer-check --unwinding-assertions --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula ) > "$OUT/cl.log" 2>&1
grep -qE '^VERIFICATION' "$OUT/cl.log" || { echo "NO VERDICT"; tail -3 "$OUT/cl.log"; }
sed -E 's/\] line [0-9]+ /] /' "$OUT/cl.log" |
  grep -E 'main\.assertion|FREE_ONCE|^VERIFICATION|loop_(invariant|assigns|decreases)|Check (invariant|decreases|assigns)' | sort -u
echo "other FAILURE: $(grep -E ': FAILURE$' "$OUT/cl.log" | grep -vcE 'main\.assertion|FREE_ONCE')"
echo "UNKNOWN: $(grep -c ': UNKNOWN$' "$OUT/cl.log")   ERROR: $(grep -c ': ERROR$' "$OUT/cl.log")"
( ulimit -v 150000000; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/c1_lc.gb" \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula ) > "$OUT/cl_vac.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/cl_vac.log" | grep 'vacuity witness'
