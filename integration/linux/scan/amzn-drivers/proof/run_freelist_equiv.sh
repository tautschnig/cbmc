#!/bin/bash
# Prove the cheap bitmask form of INV_FREE_LIST equivalent to the pairwise one.
set -u
RING=${RING:-4}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/fleq}; mkdir -p "$OUT"
rm -f "$OUT"/g0.gb "$OUT"/g1.gb "$OUT"/r0.gb "$OUT"/r1.gb
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure eth_ena_pci_probe eth_ena_pci_remove ena_populate_rx_queue \
  ena_interrupt_handler_rte ena_timer_wd_callback; do
  RM="$RM --remove-function-body $f"; done
FLAGS="--unwind $((RING + 2)) --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  D=""; [ $V = 1 ] && D=-DVACUITY_CHECK
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING=$RING $D \
      "$HERE/harness_freelist_equiv.c" "$HERE/rdtsc_stub.c" -o "$OUT/g$V.gb" \
      2>&1 | grep -i error | head )
  [ -f "$OUT/g$V.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/g$V.gb" "$OUT/r$V.gb" >/dev/null 2>&1 )
done
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r0.gb" --bounds-check \
  --pointer-check --unwinding-assertions $FLAGS ) > "$OUT/x.log" 2>&1
grep -qE '^VERIFICATION' "$OUT/x.log" || { echo "NO VERDICT"; tail -3 "$OUT/x.log"; }
grep -E '^VERIFICATION' "$OUT/x.log"
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E 'FL_EQUIV' | sort -u
echo "other FAILURE: $(grep -E ': FAILURE$' "$OUT/x.log" | grep -vcE 'FL_EQUIV')"
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r1.gb" --no-standard-checks $FLAGS ) > "$OUT/v.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/v.log" | grep 'vacuity witness'
