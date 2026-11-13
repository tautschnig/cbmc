#!/bin/bash
# Lifts the offloads == 0 restriction from the xmit step.
#
# Every earlier xmit and cleanup harness set ring.offloads = 0, which excludes
# the meta-descriptor path and, with it, ena_tx_mbuf_prepare -- where the
# application's ol_flags and header-length fields enter the driver.
#
#   noffl          offloads == 0, the previous restriction, as a control
#   offl           offloads and the mbuf metadata nondeterministic
#   offl_contract  the same, plus DPDK's TSO contract that the header lengths
#                  describe this packet
#
# Each log records its cbmc invocation on a CBMC-CMD: line so check_hygiene.sh
# can tell a sliced-away unwinding assertion from a missing flag.
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/xoff}; mkdir -p "$OUT"
RING=${RING:-8}
F="--unwind $((RING + 4)) --unwindset ena_tx_map_mbuf.0:3,memcmp.0:9 \
  $SOLVER --max-field-sensitivity-array-size 256 --slice-formula \
  --unwinding-assertions"
for cfg in "noffl:" "offl:-DWITH_OFFLOADS" \
           "offl_contract:-DWITH_OFFLOADS -DASSUME_HEADERS_INSIDE_PACKET"; do
  n=${cfg%%:*}; d=${cfg#*:}
  for V in 0 1; do
    vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
    rm -f "$OUT/$n$V.gb"
    ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
      "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING=$RING \
        -DPART=1 -DASSUME_PREPARE_TX_OK $d $vd \
        "$HERE/harness_xmit_step_offloads.c" "$HERE/rdtsc_stub.c" \
        -o "$OUT/$n$V.gb" 2>&1 | grep -i " error" | head -3 )
    [ -f "$OUT/$n$V.gb" ] || { echo "$n$V: BUILD FAILED"; exit 1; }
  done
  echo "CBMC-CMD: cbmc $OUT/${n}0.gb --bounds-check --pointer-check $F" > "$OUT/$n.log"
  ( ulimit -v ${MEM:-50000000}; timeout ${TO:-2700} "$BIN/cbmc" "$OUT/${n}0.gb" \
      --bounds-check --pointer-check $F ) >> "$OUT/$n.log" 2>&1
  echo "CBMC-CMD: cbmc $OUT/${n}1.gb --no-standard-checks $F" > "$OUT/${n}_v.log"
  ( ulimit -v ${MEM:-50000000}; timeout ${TO:-2700} "$BIN/cbmc" "$OUT/${n}1.gb" \
      --no-standard-checks $F ) >> "$OUT/${n}_v.log" 2>&1
  echo "== $n"
  grep -E '^VERIFICATION' "$OUT/$n.log" | sed 's/^/   /'
  grep -E ': FAILURE$' "$OUT/$n.log" | sed -E 's/\] line [0-9]+ /] /' | cut -c1-110 | sed 's/^/   /' | head -3
  printf '   UNKNOWN: %s   no-body: %s\n' \
    "$(grep -cE ': UNKNOWN$' "$OUT/$n.log")" \
    "$(grep -cE 'no body for callee.*FAILURE' "$OUT/$n.log")"
done
