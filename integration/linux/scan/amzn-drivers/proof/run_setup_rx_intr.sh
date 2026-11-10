#!/bin/bash
# F18: ena_setup_rx_intr returns 0 after failing and undoing its own setup.
set -u
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/rxintr}; mkdir -p "$OUT"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure eth_ena_pci_probe eth_ena_pci_remove ena_populate_rx_queue \
  ena_interrupt_handler_rte ena_timer_wd_callback ena_rx_queue_intr_disable; do
  RM="$RM --remove-function-body $f"; done
for V in 0 1; do
  D="${DEFS:-}"; [ $V = 1 ] && D="$D -DVACUITY_CHECK"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC $D \
      "$HERE/harness_setup_rx_intr.c" "$HERE/rdtsc_stub.c" -o "$OUT/i$V.gb" \
      2>&1 | grep -i error | head ) || exit 1
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/i$V.gb" "$OUT/r$V.gb" >/dev/null 2>&1 )
done
( ulimit -v 60000000; timeout 1200 "$BIN/cbmc" "$OUT/r0.gb" --bounds-check \
  --pointer-check --unwind 8 --unwinding-assertions --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula --trace ) > "$OUT/x.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E 'F18_SETUP_RC|^VERIFICATION'
echo "other FAILURE: $(grep -E ': FAILURE$' "$OUT/x.log" | grep -vcE 'F18_')"
echo "no-body reached: $(grep -cE 'no body for callee.*FAILURE' "$OUT/x.log")   unwind FAIL: $(grep -cE 'unwind.*: FAILURE$' "$OUT/x.log")"
( ulimit -v 60000000; timeout 1200 "$BIN/cbmc" "$OUT/r1.gb" --unwind 8 \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula ) > "$OUT/v.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/v.log" | grep 'vacuity witness'
