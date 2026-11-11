#!/bin/bash
# The cleanup loop invariant by hand, in three machine-checked parts, with no
# dependence on the ring size.
set -u
RING=${RING:-8}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/clstep}; mkdir -p "$OUT"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure eth_ena_pci_probe eth_ena_pci_remove ena_populate_rx_queue \
  ena_interrupt_handler_rte ena_timer_wd_callback; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind $((RING + 3)) --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula"
for P in 1 2 3; do
  for V in 0 1; do
    D=""; [ $V = 1 ] && D=-DVACUITY_CHECK
    rm -f "$OUT/p$P$V.gb" "$OUT/r$P$V.gb"
    ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
      "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING=$RING \
        -DPART=$P $D "$HERE/harness_cleanup_step.c" "$HERE/rdtsc_stub.c" \
        -o "$OUT/p$P$V.gb" 2>&1 | grep -i error | head )
    [ -f "$OUT/p$P$V.gb" ] || { echo "BUILD FAILED part $P, refusing to run stale binaries"; exit 1; }
    ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/p$P$V.gb" "$OUT/r$P$V.gb" >/dev/null 2>&1 )
  done
  ( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r${P}0.gb" \
    --bounds-check --pointer-check --unwinding-assertions $FLAGS ) > "$OUT/x$P.log" 2>&1
  printf 'part %s  %s\n' "$P" "$(grep -oE 'VERIFICATION [A-Z]+' "$OUT/x$P.log" | head -1 || echo 'NO VERDICT')"
  sed -E 's/\] line [0-9]+ /] /; s/^/    /' "$OUT/x$P.log" | grep -E 'CS_' | sort -u
  printf '    other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
    "$(grep -E ': FAILURE$' "$OUT/x$P.log" | grep -vcE 'CS_')" \
    "$(grep -cE 'no body for callee.*FAILURE' "$OUT/x$P.log")" \
    "$(grep -c ': UNKNOWN$' "$OUT/x$P.log")" \
    "$(grep -cE 'unwind.*: FAILURE$' "$OUT/x$P.log")"
  ( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r${P}1.gb" \
    --no-standard-checks $FLAGS ) > "$OUT/v$P.log" 2>&1
  sed -E 's/\] line [0-9]+ /] /; s/^/    /' "$OUT/v$P.log" | grep 'vacuity witness'
done
