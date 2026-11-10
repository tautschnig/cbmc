#!/bin/bash
# Async operation 2: the AENQ dispatch over a device-written ring.
set -u
QD=${QD:-4}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/aenq}; mkdir -p "$OUT"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure ena_rss_hash_conf_get ena_rss_reta_query ena_rss_hash_update \
  ena_rss_reta_update eth_ena_pci_probe eth_ena_pci_remove ena_populate_rx_queue \
  ena_interrupt_handler_rte ena_timer_wd_callback ena_com_execute_admin_command \
  ena_com_wait_and_process_admin_cq; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind $((QD + 16)) --unwindset ena_com_aenq_intr_handler.0:$((QD + 2)),ena_suboptimal_configuration.1:65 \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula"
rm -f "$OUT"/a0.gb "$OUT"/a1.gb "$OUT"/r0.gb "$OUT"/r1.gb
for V in 0 1; do
  D="${DEFS:-}"; [ $V = 1 ] && D="$D -DVACUITY_CHECK"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DQD=$QD \
      $D "$HERE/harness_aenq.c" "$HERE/rdtsc_stub.c" -o "$OUT/a$V.gb" \
      2>&1 | grep -i error | head ) || exit 1
  [ -f "$OUT/a$V.gb" ] || { echo "BUILD FAILED for V=$V (refusing to run stale binaries)"; exit 1; }
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM \
      --replace-calls ena_link_update:model_link_update \
      "$OUT/a$V.gb" "$OUT/r$V.gb" >/dev/null 2>&1 )
done
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r0.gb" --bounds-check \
  --pointer-check --div-by-zero-check --unwinding-assertions $FLAGS ) > "$OUT/x.log" 2>&1
grep -qE '^VERIFICATION' "$OUT/x.log" || { echo "NO VERDICT"; tail -3 "$OUT/x.log"; }
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E 'AQ_|^VERIFICATION' | sort -u
echo "other FAILURE: $(grep -E ': FAILURE$' "$OUT/x.log" | grep -vcE 'AQ_')"
echo "no-body reached: $(grep -cE 'no body for callee.*FAILURE' "$OUT/x.log")"
echo "UNKNOWN: $(grep -c ': UNKNOWN$' "$OUT/x.log")   ERROR: $(grep -c ': ERROR$' "$OUT/x.log")   unwind FAIL: $(grep -cE 'unwind.*: FAILURE$' "$OUT/x.log")"
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r1.gb" --no-standard-checks $FLAGS ) > "$OUT/v.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/v.log" | grep 'vacuity witness'
