#!/bin/bash
# Config/query phase: ena_xstats_get_by_id with application-supplied indices.
set -u
NIDS=${NIDS:-2}; NQ=${NQ:-2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/xs}; mkdir -p "$OUT"
rm -f "$OUT"/g0.gb "$OUT"/g1.gb
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup __rte_raw_cksum ena_rss_configure \
  ena_rss_hash_conf_get ena_rss_reta_query ena_rss_hash_update \
  ena_rss_reta_update eth_ena_pci_probe eth_ena_pci_remove \
  ena_populate_rx_queue ena_interrupt_handler_rte ena_timer_wd_callback; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind $((NIDS + 10)) --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  D="${DEFS:-}"; [ $V = 1 ] && D="$D -DVACUITY_CHECK"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DNIDS=$NIDS -DNQ=$NQ \
      $D "$HERE/harness_xstats.c" "$HERE/rdtsc_stub.c" -o "$OUT/g$V.gb" \
      2>&1 | grep -i error | head )
  [ -f "$OUT/g$V.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM --replace-calls ena_copy_customer_metrics:model_copy_customer_metrics --replace-calls ena_copy_ena_srd_info:model_copy_ena_srd_info "$OUT/g$V.gb" "$OUT/r$V.gb" >/dev/null 2>&1 )
done
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r0.gb" --bounds-check \
  --pointer-check --div-by-zero-check --unwinding-assertions $FLAGS ) > "$OUT/x.log" 2>&1
grep -qE '^VERIFICATION' "$OUT/x.log" || { echo "NO VERDICT"; tail -3 "$OUT/x.log"; }
grep -E '^VERIFICATION' "$OUT/x.log"
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E 'XS_' | sort -u
echo "failing properties: $(grep -cE ': FAILURE$' "$OUT/x.log")"
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E ': FAILURE$' | sort -u | head -5
echo "division properties: $(grep -ci 'division' "$OUT/x.log")"
echo "no-body reached: $(grep -cE 'no body for callee.*FAILURE' "$OUT/x.log")   UNKNOWN: $(grep -c ': UNKNOWN$' "$OUT/x.log")"
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r1.gb" --no-standard-checks $FLAGS ) > "$OUT/v.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/v.log" | grep 'vacuity witness'
