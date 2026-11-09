#!/bin/bash
# The ena_com_prepare_tx contract the xmit step stubs, checked against the
# real ena_eth_com.c from an arbitrary between-packets SQ state.
set -u
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/prepare_tx}
mkdir -p "$W"
FLAGS="--unwind 20 --unwindset memcmp.0:9 --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula"
run() {
  local name=$1; shift
  ( cd /tmp/dpdk/build-ql && ulimit -v 16000000 &&
    "$B/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC "$@" \
      "$P/harness_prepare_tx.c" -o "$W/$name.gb" ) || return 1
  local chk="--bounds-check --pointer-check --signed-overflow-check \
    --undefined-shift-check --unwinding-assertions --trace"
  case "$*" in *VACUITY*) chk="";; esac
  ( ulimit -v 60000000; timeout 2400 "$B/cbmc" "$W/$name.gb" $FLAGS $chk ) \
    > "$W/$name.log" 2>&1
  printf '%-16s %s\n' "$name" "$(grep -E '^VERIFICATION' "$W/$name.log")"
  sed -E 's/\] line [0-9]+ /] /; s/^/    /' "$W/$name.log" |
    grep -E 'PTX_|witness|: FAILURE$' | sort -u
  case "$*" in *VACUITY*) ;; *)
    echo "    unwinding FAILURE: $(grep -cE 'unwind.*: FAILURE$' "$W/$name.log")";;
  esac
}
run real
run no_f14        -DASSUME_NO_F14
run no_f14_vac    -DASSUME_NO_F14 -DVACUITY_CHECK
run fixed         -DFIXED_ESTIMATE
run fixed_vac     -DFIXED_ESTIMATE -DVACUITY_CHECK
run nb_positive   -DASSUME_NO_F14 -DCLAIM_NB_POSITIVE
