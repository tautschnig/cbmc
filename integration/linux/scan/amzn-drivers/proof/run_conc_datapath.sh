#!/bin/bash
# Concurrency pair 3: the TX datapath against a control-path stop.
#
# ena_queue_stop destroys the io queue and THEN releases the ring's mbufs
# (ena_ethdev.c:1474-1485), so there is a window between the two.  WHEN selects
# where the transmit lands: 0 before the destroy, 1 in the window, 2 after the
# release.
#
# Three configurations, because there turn out to be TWO independent defects
# and fixing either alone is not enough:
#
#   asis           the driver as built: the state check in eth_ena_xmit_pkts is
#                  under RTE_ETHDEV_DEBUG_TX, which a normal build does not
#                  set -- the same shape as F16's RTE_ASSERT
#   checked        the check compiled back in.  STILL FAILS, because ena_stop
#                  assigns ENA_ADAPTER_STATE_STOPPED nineteen lines AFTER it
#                  destroys the queues (ena_ethdev.c:1379-1406), so the state
#                  still reads RUNNING throughout the teardown window
#   checked-early  the check compiled in AND the state left before the
#                  teardown.  Holds at every interleaving point.
#
# ena_start orders it the safe way round -- queues started, then RUNNING
# assigned (:1323-1347) -- so the convention exists and the stop path departs
# from it.
#
# --no-pointer-check is deliberate: pointer checks over these union-bearing
# ena_com structs leave io_sq field reads undecided, as recorded in
# run_doorbell_budget.sh.  Bounds and signed-overflow checking stay on.
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/cdp}; mkdir -p "$OUT"
RING=${RING:-4}
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_tx_cleanup \
  __rte_raw_cksum eth_ena_pci_probe eth_ena_pci_remove ena_populate_rx_queue \
  ena_interrupt_handler_rte ena_timer_wd_callback ena_xstats_get \
  ena_xstats_get_names ena_xstats_get_by_id; do
  RM="$RM --remove-function-body $f"; done
F="--max-field-sensitivity-array-size 256 --unwind 20 \
  --unwindset ena_tx_map_mbuf.0:3,memcmp.0:9 $SOLVER --slice-formula \
  --unwinding-assertions"
for cfg in "asis:" "checked:-DWITH_STATE_CHECK" \
           "checked-early:-DWITH_STATE_CHECK -DWITH_EARLY_STATE"; do
  c=${cfg%%:*}; cd_=${cfg#*:}
  for W in ${WHENS:-0 1 2}; do
    for V in 0 1; do
      vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
      rm -f "$OUT/$c$W$V.gb" "$OUT/r$c$W$V.gb"
      ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
        "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING=$RING \
          -DWHEN=$W $cd_ $vd "$HERE/harness_conc_datapath.c" "$HERE/rdtsc_stub.c" \
          -o "$OUT/$c$W$V.gb" 2>&1 | grep -i " error" | head -3 )
      [ -f "$OUT/$c$W$V.gb" ] || { echo "$c WHEN=$W: BUILD FAILED"; exit 1; }
      ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/$c$W$V.gb" "$OUT/r$c$W$V.gb" >/dev/null 2>&1 )
    done
    echo "CBMC-CMD: cbmc $OUT/r${c}${W}0.gb --bounds-check --no-pointer-check --signed-overflow-check $F" > "$OUT/$c$W.log"
    ( ulimit -v ${MEM:-60000000}; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r${c}${W}0.gb" \
        --bounds-check --no-pointer-check --signed-overflow-check $F ) >> "$OUT/$c$W.log" 2>&1
    echo "CBMC-CMD: cbmc $OUT/r${c}${W}1.gb --no-standard-checks $F" > "$OUT/$c${W}_v.log"
    ( ulimit -v ${MEM:-60000000}; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r${c}${W}1.gb" \
        --no-standard-checks $F ) >> "$OUT/$c${W}_v.log" 2>&1
    printf '== %-8s WHEN=%s  ' "$c" "$W"
    grep -oE 'VERIFICATION [A-Z]+' "$OUT/$c$W.log" | head -1
    sed -E 's/\] line [0-9]+ /] /; s/^/     /' "$OUT/$c$W.log" | grep -E 'CC3_' | sort -u
    printf '     other FAILURE: %s   no-body: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
      "$(grep -E ': FAILURE$' "$OUT/$c$W.log" | grep -vcE 'CC3_|witness')" \
      "$(grep -cE 'no body for callee.*FAILURE' "$OUT/$c$W.log")" \
      "$(grep -cE ': UNKNOWN$' "$OUT/$c$W.log")" \
      "$(grep -cE 'unwind.*: FAILURE' "$OUT/$c$W.log")"
    sed -E 's/\] line [0-9]+ /] /; s/^/     /' "$OUT/$c${W}_v.log" | grep -E '\bwitness\b' | sort -u
  done
done
