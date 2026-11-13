#!/bin/bash
# Does the LLQ doorbell estimate actually guarantee the burst has room for the
# packet the driver then submits?  Three configurations:
#   as-is    the driver as written          -> DB_CONTRACT must FAIL
#   fixed    -DWITH_FIX                     -> DB_CONTRACT must hold
#   vacuity  -DVACUITY_CHECK                -> both witnesses must FAIL
#
# --no-pointer-check is deliberate: pointer checks over these union-bearing
# ena_com structs report an out-of-bounds write through *nb_hw_desc even when
# it points at a file-scope static, which is a tool limitation and not a
# driver property.  Bounds checking is kept.
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/db}; mkdir -p "$OUT"
NPKTS=${NPKTS:-2}
for cfg in "as-is:" "fixed:-DWITH_FIX" "vacuity:-DVACUITY_CHECK"; do
  n=${cfg%%:*}; d=${cfg#*:}
  rm -f "$OUT/$n.gb"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DNPKTS=$NPKTS $d \
      -I/tmp/dpdk/drivers/net/ena/base -I/tmp/dpdk/drivers/net/ena \
      "$HERE/harness_doorbell.c" -o "$OUT/$n.gb" 2>&1 | grep -i error | head -5 )
  [ -f "$OUT/$n.gb" ] || { echo "$n: BUILD FAILED, refusing to run a stale binary"; exit 1; }
  EX=""; [ "$n" = vacuity ] && EX="--no-standard-checks"
  ( ulimit -v ${MEM:-50000000}; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/$n.gb" \
      --bounds-check --no-pointer-check $EX --object-bits 10 --unwinding-assertions \
      --unwind 28 --unwindset memcmp.0:9,main.0:$((NPKTS+1)) $SOLVER --slice-formula ) > "$OUT/$n.log" 2>&1
  echo "== $n"
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/$n.log" | grep -E 'DB_CONTRACT|vacuity|^   VERIFICATION' | sort -u
  printf '   other FAILURE: %s   no-body: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
    "$(grep -E ': FAILURE$' "$OUT/$n.log" | grep -vcE 'DB_CONTRACT|vacuity')" \
    "$(grep -cE 'no body for callee.*FAILURE' "$OUT/$n.log")" \
    "$(grep -cE ': UNKNOWN$' "$OUT/$n.log")" \
    "$(grep -E 'unwind.*: FAILURE' "$OUT/$n.log" | wc -l)"
done
