#!/bin/bash
# F19: ena_rss_reta_query reads past its stack indirection table when
# reta_size exceeds ENA_RX_RSS_TABLE_SIZE, which only the update path checks.
set -u
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/rss}; mkdir -p "$OUT"
rm -f "$OUT"/g0.gb "$OUT"/g1.gb
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  eth_ena_pci_probe eth_ena_pci_remove ena_populate_rx_queue \
  ena_interrupt_handler_rte ena_timer_wd_callback; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind 600 --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  D="${DEFS:-}"; [ $V = 1 ] && D="$D -DVACUITY_CHECK"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC $D \
      "$HERE/harness_rss_reta.c" "$HERE/rdtsc_stub.c" -o "$OUT/g$V.gb" \
      2>&1 | grep -i error | head )
  [ -f "$OUT/g$V.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM --replace-calls ena_mp_indirect_table_get:model_indirect_table_get "$OUT/g$V.gb" "$OUT/r$V.gb" >/dev/null 2>&1 )
done
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r0.gb" --bounds-check \
  --pointer-check $FLAGS ) > "$OUT/x.log" 2>&1
grep -qE '^VERIFICATION' "$OUT/x.log" || { echo "NO VERDICT"; tail -3 "$OUT/x.log"; }
grep -E '^VERIFICATION' "$OUT/x.log"
echo "out-of-bounds reads of indirect_table:"
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E "indirect_table.*: (FAILURE|SUCCESS)" | sort -u
echo "all failing properties:"
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E ': FAILURE$' | sort -u | head -6
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r1.gb" --no-standard-checks $FLAGS ) > "$OUT/v.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/v.log" | grep 'vacuity witness'
