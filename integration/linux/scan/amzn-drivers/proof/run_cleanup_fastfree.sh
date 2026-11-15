#!/bin/bash
# The fast-free branch of ena_tx_cleanup: is the pkts_to_clean write in bounds,
# and on what does that depend?
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
OUT=${OUT:-/tmp/ena-proof/ff}; mkdir -p "$OUT"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure eth_ena_pci_probe eth_ena_pci_remove ena_populate_rx_queue \
  ena_interrupt_handler_rte ena_timer_wd_callback; do
  RM="$RM --remove-function-body $f"; done
for cfg in "any_mbuf:" "well_formed:-DASSUME_WELL_FORMED_MBUF"; do
  n=${cfg%%:*}; d=${cfg#*:}
  echo "== $n"
  for V in 0 1; do
    vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
    rm -f "$OUT/$n$V.gb" "$OUT/r$n$V.gb"
    ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
      "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC $d $vd \
        "$HERE/harness_cleanup_fastfree.c" "$HERE/rdtsc_stub.c" -o "$OUT/$n$V.gb" \
        2>&1 | grep -i error | head )
    [ -f "$OUT/$n$V.gb" ] || { echo "   BUILD FAILED, refusing to run stale binaries"; exit 1; }
    ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/$n$V.gb" "$OUT/r$n$V.gb" >/dev/null 2>&1 )
  done
  ( ulimit -v 60000000; timeout ${TO:-900} "$BIN/cbmc" $SPIN_CAP "$OUT/r${n}0.gb" --bounds-check \
    --pointer-check --unwinding-assertions --unwind 4 $SOLVER --slice-formula ) > "$OUT/$n.log" 2>&1
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/$n.log" | grep -E 'FF_|^   VERIFICATION' | sort -u
  printf '   other FAILURE: %s   no-body reached: %s\n' \
    "$(grep -E ': FAILURE$' "$OUT/$n.log" | grep -vcE 'FF_')" \
    "$(grep -cE 'no body for callee.*FAILURE' "$OUT/$n.log")"
  ( ulimit -v 60000000; timeout ${TO:-900} "$BIN/cbmc" $SPIN_CAP "$OUT/r${n}1.gb" --no-standard-checks \
    --unwind 4 $SOLVER --slice-formula ) > "$OUT/${n}_v.log" 2>&1
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/${n}_v.log" | grep 'vacuity witness'
done
