#!/bin/bash
# End-to-end LLQ path on the real code: device init -> tx queue setup ->
# queue start -> eth_ena_xmit_pkts.  Usage:
#   run_llq_path.sh devinit            # -DDEVINIT_ONLY (#4)
#   run_llq_path.sh xmit [DEFINES...]  # PREPARE_OK, plus the vacuity build
set -u
MODE=${1:-xmit}; shift || true
RING=${RING:-8}
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/llq-$MODE}
mkdir -p "$W"
DEFS="-DVACUITY_CHECK=0 -DRING=$RING -DBURST=1 -DSEGS=1 $*"
[ "$MODE" = devinit ] && DEFS="$DEFS -DDEVINIT_ONLY"
FLAGS="--unwind 10 --unwindset eth_ena_xmit_pkts.0:3,ena_tx_map_mbuf.0:3,memcmp.0:9 \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula"
build() {
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$B/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC $DEFS "$@" \
      "$P/harness_llq_path.c" "$P/rdtsc_stub.c" "$P/pktmbuf_read_stub.c" \
      -o "$W/raw.gb" ) || exit 1
  ( ulimit -v 32000000; "$B/goto-instrument" $(cat "$P/llq_path_rm.txt") \
      --replace-calls ena_com_execute_admin_command:model_execute_admin_command \
      --replace-calls ena_mem_alloc_coherent:model_mem_alloc_coherent \
      "$W/raw.gb" "$W/$1.gb" >/dev/null 2>&1 ) || exit 1
}
build main
( ulimit -v 150000000; timeout 3600 "$B/cbmc" "$W/main.gb" --bounds-check \
  --pointer-check --unwinding-assertions $FLAGS ) > "$W/main.log" 2>&1
# in devinit mode everything after device init is unreachable, so only the
# DEVINIT obligations and the init witness mean anything
if [ "$MODE" = devinit ]; then SEL="DEVINIT|^VERIFICATION"; VSEL="device init completes"
else SEL="^\[main\.assertion|LLQ line copy|^VERIFICATION"; VSEL="vacuity witness"; fi
sed -E 's/\] line [0-9]+ /] /' "$W/main.log" | grep -E "$SEL"
echo "other FAILURE: $(grep -E ': FAILURE$' "$W/main.log" | grep -vc 'main\.assertion')"
echo "unwinding: $(grep -cE 'unwind.*: SUCCESS$' "$W/main.log") SUCCESS, $(grep -cE 'unwind.*: FAILURE$' "$W/main.log") FAILURE"
echo "no-body FAILURE: $(grep -cE 'no body for callee.*FAILURE' "$W/main.log")"
DEFS="${DEFS/-DVACUITY_CHECK=0/-DVACUITY_CHECK=1}"; build vac
( ulimit -v 150000000; timeout 3600 "$B/cbmc" "$W/vac.gb" $FLAGS ) > "$W/vac.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$W/vac.log" | grep -E "$VSEL"
