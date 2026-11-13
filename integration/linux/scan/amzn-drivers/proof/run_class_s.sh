#!/bin/bash
# Class S: for each setter, does the return code reflect what the device did?
#
# The largest group the Unveil class list had no name for -- an operation the
# device REJECTED reported as SUCCESS -- holds 6 of the 40 class Z findings
# plus two of our own (F18, F21).  Two obligations of this shape already
# existed (F18_SETUP_RC, CFG_AENQ_FAIL); this applies it to entry points that
# had none.
#
#   ENTRY 0  ena_mtu_set           must HOLD (DPDK propagates rc)
#   ENTRY 1  ena_rss_reta_update   must HOLD
#   ENTRY 2  ena_rx_queue_intr_enable  must FAIL -- it returns 0
#            unconditionally because ena_rx_queue_intr_set is void, so F17 is
#            a class S finding as well as a device-access one
#   ENTRY 3  ena_stop              must FAIL -- F26, a requested device reset
#            that fails is logged and discarded, and the teardown proceeds
#   ENTRY 4  ena_rss_hash_update   must HOLD
#
# ENTRY 2 runs with --no-pointer-check: pointer checks over these
# union-bearing ena_com structs leave the intr_reg dereferences UNDECIDED,
# the same tool limitation documented in run_doorbell_budget.sh.  The
# obligation is about the return code, and bounds checking stays on.
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/cls}; mkdir -p "$OUT"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup __rte_raw_cksum eth_ena_pci_probe \
  eth_ena_pci_remove ena_populate_rx_queue ena_interrupt_handler_rte \
  ena_timer_wd_callback ena_xstats_get ena_xstats_get_names; do
  RM="$RM --remove-function-body $f"; done
F="--max-field-sensitivity-array-size 256 --unwind 600 $SOLVER \
  --slice-formula --unwinding-assertions"
for E in ${ENTRIES:-0 1 2 3 4}; do
  PC="--pointer-check"; [ "$E" = 2 ] && PC="--no-pointer-check"
  for V in 0 1; do
    vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
    rm -f "$OUT/e$E$V.gb" "$OUT/r$E$V.gb"
    ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
      "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DENTRY=$E $vd \
        "$HERE/harness_class_s.c" "$HERE/rdtsc_stub.c" -o "$OUT/e$E$V.gb" \
        2>&1 | grep -i " error" | head -3 )
    [ -f "$OUT/e$E$V.gb" ] || { echo "ENTRY=$E: BUILD FAILED"; exit 1; }
    ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/e$E$V.gb" "$OUT/r$E$V.gb" >/dev/null 2>&1 )
  done
  echo "CBMC-CMD: cbmc $OUT/r${E}0.gb --bounds-check $PC $F" > "$OUT/x$E.log"
  ( ulimit -v ${MEM:-60000000}; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r${E}0.gb" \
      --bounds-check $PC $F ) >> "$OUT/x$E.log" 2>&1
  echo "CBMC-CMD: cbmc $OUT/r${E}1.gb --no-standard-checks $F" > "$OUT/v$E.log"
  ( ulimit -v ${MEM:-60000000}; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/r${E}1.gb" \
      --no-standard-checks $F ) >> "$OUT/v$E.log" 2>&1
  echo "== ENTRY=$E"
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/x$E.log" | grep -E 'SM_|^   VERIFICATION' | sort -u
  printf '   other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
    "$(grep -E ': FAILURE$' "$OUT/x$E.log" | grep -vcE 'SM_|witness')" \
    "$(grep -cE 'no body for callee.*FAILURE' "$OUT/x$E.log")" \
    "$(grep -cE ': UNKNOWN$' "$OUT/x$E.log")" \
    "$(grep -cE 'unwind.*: FAILURE' "$OUT/x$E.log")"
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/v$E.log" | grep -E '\bwitness\b' | sort -u
done
