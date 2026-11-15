#!/bin/bash
# Class E, deadlock and lockup: the admin-lock discipline across every entry
# point that takes it.  One lock means no lock ORDER to violate; the hazard is
# a path that returns while still holding it, which locks up the control plane.
#
#   ENTRY 0 ena_rss_reta_update   (unlocks in two places, one an error path)
#   ENTRY 1 ena_rss_reta_query
#   ENTRY 2 ena_rss_hash_update
#   ENTRY 3 ena_rss_hash_conf_get
#   ENTRY 4 ena_stats_get
#   ENTRY 5 ena_mp_primary_handle -- the IPC handler servicing a SECONDARY
#           process inside the primary.  It performs admin operations and takes
#           adapter->admin_lock nowhere, so LKO_ADMIN_LOCKED FAILS here while
#           holding for every entry point above.  Runs --no-pointer-check for
#           the same union-bearing-struct reason as ENTRY 2.
#
# -DBREAK_RETA_UNLOCK is the mutation test: it deletes the error-path unlock
# so the obligations must catch it.  A proof that cannot fail is not a proof.
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
OUT=${OUT:-/tmp/ena-proof/lko}; mkdir -p "$OUT"
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup __rte_raw_cksum eth_ena_pci_probe \
  eth_ena_pci_remove ena_populate_rx_queue ena_interrupt_handler_rte \
  ena_timer_wd_callback ; do
  RM="$RM --remove-function-body $f"; done
# ena_mp_indirect_table_set keeps its body deliberately: removing it cut the
# path right where the normal-path unlock lives, which left the obligations
# vacuous.  The EAL primitives it calls are modelled in the harness instead.
F="--unwind ${UNWIND:-300} $SOLVER --max-field-sensitivity-array-size 256 \
  --slice-formula --unwinding-assertions"
# All five entry points.  The ena_com_* admin calls each makes while holding
# the lock are modelled in the harness with arbitrary returns, so both the
# success and the failure branch stay reachable -- the failure branches are
# where a missing unlock would live.
for E in ${ENTRIES:-0 1 2 3 4 5}; do
 for cfg in "ok:" "mutated:-DBREAK_RETA_UNLOCK"; do
  n=${cfg%%:*}; d=${cfg#*:}
  [ "$n" = mutated ] && [ "$E" != 0 ] && continue
  for V in 0 1; do
    vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
    rm -f "$OUT/e$E$n$V.gb" "$OUT/r$E$n$V.gb"
    ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
      "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DENTRY=$E $d $vd \
        "$HERE/harness_lock_order.c" -o "$OUT/e$E$n$V.gb" 2>&1 | grep -i " error" | head -3 )
    [ -f "$OUT/e$E$n$V.gb" ] || { echo "ENTRY=$E $n: BUILD FAILED"; exit 1; }
    ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/e$E$n$V.gb" "$OUT/r$E$n$V.gb" >/dev/null 2>&1 )
  done
  log="$OUT/e$E$n.log"
  PC=""; { [ "$E" = 2 ] || [ "$E" = 5 ]; } && PC="--no-pointer-check"
  echo "CBMC-CMD: cbmc $OUT/r${E}${n}0.gb --bounds-check $PC $F" > "$log"
  ( ulimit -v ${MEM:-48000000}; timeout ${TO:-1800} "$BIN/cbmc" $SPIN_CAP "$OUT/r${E}${n}0.gb" \
      --bounds-check $PC $F ) >> "$log" 2>&1
  vlog="$OUT/e$E${n}_v.log"
  echo "CBMC-CMD: cbmc $OUT/r${E}${n}1.gb --no-standard-checks $F" > "$vlog"
  ( ulimit -v ${MEM:-48000000}; timeout ${TO:-1800} "$BIN/cbmc" $SPIN_CAP "$OUT/r${E}${n}1.gb" \
      --no-standard-checks $F ) >> "$vlog" 2>&1
  echo "== ENTRY=$E $n"
  grep -E 'LKO_|^VERIFICATION' "$log" | sed -E 's/\] line [0-9]+ /] /' | sort -u | sed 's/^/   /'
  printf '   other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
    "$(grep -E ': FAILURE$' "$log" | grep -vcE 'LKO_|witness')" \
    "$(grep -cE 'no body for callee.*FAILURE' "$log")" \
    "$(grep -cE ': UNKNOWN$' "$log")" \
    "$(grep -cE 'unwind.*: FAILURE' "$log")"
  grep -E 'witness' "$vlog" | sed -E 's/\] line [0-9]+ /] /' | sort -u | sed 's/^/   /'
 done
done
