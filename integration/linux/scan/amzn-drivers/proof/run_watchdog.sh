#!/bin/bash
# Async operation 1: the watchdog timer ena_timer_wd_callback.
set -u
RING=${RING:-4}; NQ=${NQ:-2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/wd}; mkdir -p "$OUT"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure ena_rss_hash_conf_get ena_rss_reta_query ena_rss_hash_update \
  ena_rss_reta_update eth_ena_pci_probe eth_ena_pci_remove ena_populate_rx_queue \
  ena_interrupt_handler_rte; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind $((RING + 3)) --unwindset check_for_tx_completions.0:$((NQ + 2)),check_for_tx_completion_in_queue.0:$((RING + 1)) \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  D="${DEFS:-}"; [ $V = 1 ] && D="$D -DVACUITY_CHECK"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING=$RING -DNQ=$NQ \
      $D "$HERE/harness_watchdog.c" "$HERE/rdtsc_stub.c" -o "$OUT/w$V.gb" \
      2>&1 | grep -i error | head ) || exit 1
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/w$V.gb" "$OUT/r$V.gb" >/dev/null 2>&1 )
done
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r0.gb" --bounds-check --div-by-zero-check \
  --pointer-check --unwinding-assertions $FLAGS ) > "$OUT/x.log" 2>&1
grep -qE '^VERIFICATION' "$OUT/x.log" || { echo "NO VERDICT"; tail -3 "$OUT/x.log"; }
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E 'WD_|^VERIFICATION' | sort -u
echo "other FAILURE: $(grep -E ': FAILURE$' "$OUT/x.log" | grep -vcE 'WD_')"
echo "no-body reached: $(grep -cE 'no body for callee.*FAILURE' "$OUT/x.log")"
echo "UNKNOWN: $(grep -c ': UNKNOWN$' "$OUT/x.log")   ERROR: $(grep -c ': ERROR$' "$OUT/x.log")   unwind FAIL: $(grep -cE 'unwind.*: FAILURE$' "$OUT/x.log")"
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r1.gb" --no-standard-checks $FLAGS ) > "$OUT/v.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/v.log" | grep 'vacuity witness'
