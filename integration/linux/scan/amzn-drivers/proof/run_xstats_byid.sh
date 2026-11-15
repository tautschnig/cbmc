#!/bin/bash
# F27: ena_xstats_get_by_id fetches the ENA-SRD statistics into a local and
# then returns a different object's contents (ena_ethdev.c:3650-3665).  The
# full ena_xstats_get reads the local correctly (:3574), which is what makes
# this a mistake rather than a design.
#
#   as-is   XB_SRD_FRESH must FAIL
#   fixed   -DWITH_SRD_FIX models reading the local; it must hold
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
# Cap the two spin loops of DPDK's x86 rte_spinlock_lock asm.  Under the
# asm-semantics build symex cannot decide them and unwinds both to --unwind
# (cbmc-limitations/asm_spinlock_unwinds.md).  Sound with unwinding
# assertions on (the CBMC 6 default): a lock that could spin fails one.
# SPIN_CAP= disables it.
SPIN_CAP=${SPIN_CAP-"--unwindset rte_spinlock_lock.0:2,rte_spinlock_lock.1:2"}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/xbid}; mkdir -p "$OUT"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup __rte_raw_cksum eth_ena_pci_probe \
  eth_ena_pci_remove ena_populate_rx_queue ena_interrupt_handler_rte \
  ena_timer_wd_callback; do
  RM="$RM --remove-function-body $f"; done
F="--max-field-sensitivity-array-size 256 --unwind 60 $SOLVER \
  --slice-formula --unwinding-assertions"
for cfg in "as-is:" "fixed:-DWITH_SRD_FIX" "vacuity:-DVACUITY_CHECK"; do
  n=${cfg%%:*}; d=${cfg#*:}
  rm -f "$OUT/$n.gb" "$OUT/r-$n.gb"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC $d \
      "$HERE/harness_xstats_byid.c" "$HERE/rdtsc_stub.c" -o "$OUT/$n.gb" \
      2>&1 | grep -i " error" | head -3 )
  [ -f "$OUT/$n.gb" ] || { echo "$n: BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/$n.gb" "$OUT/r-$n.gb" >/dev/null 2>&1 )
  CK="--bounds-check --pointer-check"; [ "$n" = vacuity ] && CK="--no-standard-checks"
  echo "CBMC-CMD: cbmc $OUT/r-$n.gb $CK $F" > "$OUT/$n.log"
  ( ulimit -v ${MEM:-60000000}; timeout ${TO:-2400} "$BIN/cbmc" $SPIN_CAP "$OUT/r-$n.gb" $CK $F ) >> "$OUT/$n.log" 2>&1
  echo "== $n"
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/$n.log" | grep -E 'XB_|witness|^   VERIFICATION' | sort -u
  printf '   other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
    "$(grep -E ': FAILURE$' "$OUT/$n.log" | grep -vcE 'XB_|witness')" \
    "$(grep -cE 'no body for callee.*FAILURE' "$OUT/$n.log")" \
    "$(grep -cE ': UNKNOWN$' "$OUT/$n.log")" \
    "$(grep -cE 'unwind.*: FAILURE' "$OUT/$n.log")"
done
