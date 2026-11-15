#!/bin/bash
# The ena_com_rx_pkt contract the recv step stubs, against the real
# ena_eth_com.c with an arbitrary device completion-descriptor ring.
set -u
# Backend is a variable, not a constant: measured on five models, cadical
# loses to minisat2 on four of them and by 4.5x on one
# (doc/solver-backend-comparison-2026-11.md).  Override with
#   SOLVER='$SOLVER'   or   SOLVER=--bitwuzla
SOLVER=${SOLVER:---sat-solver minisat2}
QD=${QD:-4}; MAXB=${MAXB:-2}
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
# Cap the two spin loops of DPDK's x86 rte_spinlock_lock asm.  Under the
# asm-semantics build symex cannot decide them and unwinds both to --unwind
# (cbmc-limitations/asm_spinlock_unwinds.md).  Sound with unwinding
# assertions on (the CBMC 6 default): a lock that could spin fails one.
# SPIN_CAP= disables it.
SPIN_CAP=${SPIN_CAP-"--unwindset rte_spinlock_lock.0:2,rte_spinlock_lock.1:2"}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/rx-com}; mkdir -p "$W"
FLAGS="--unwind $((QD + 3)) $SOLVER \
  --max-field-sensitivity-array-size 256 --slice-formula"
run() {
  local name=$1; shift
  ( cd /tmp/dpdk/build-ql && ulimit -v 16000000 &&
    "$B/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DQD=$QD -DMAXB=$MAXB \
      "$@" "$P/harness_rx_com.c" -o "$W/$name.gb" 2>&1 | grep -i error | head ) || return 1
  # no --conversion-check: ena_com_rx_pkt advances the u16 SQ consumer by
  # intentional modular arithmetic, which that check flags as a failure.
  local chk="--bounds-check --pointer-check --signed-overflow-check \
    --undefined-shift-check --unwinding-assertions"
  case "$*" in *VACUITY*) chk="";; esac
  ( ulimit -v 90000000; timeout 2400 "$B/cbmc" $SPIN_CAP "$W/$name.gb" $FLAGS $chk ) \
    > "$W/$name.log" 2>&1
  printf '%-10s %s\n' "$name" "$(grep -E '^VERIFICATION' "$W/$name.log")"
  sed -E 's/\] line [0-9]+ /] /; s/^/    /' "$W/$name.log" |
    grep -E 'RXC_|vacuity witness' | sort -u
  case "$*" in *VACUITY*) ;; *)
    echo "    other FAILURE: $(grep -cE ': FAILURE$' "$W/$name.log" | head -1)   UNKNOWN: $(grep -c ': UNKNOWN$' "$W/$name.log")   unwind FAIL: $(grep -cE 'unwind.*: FAILURE$' "$W/$name.log")";;
  esac
}
run main
run vacuity -DVACUITY_CHECK
