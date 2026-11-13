#!/bin/bash
# eth_ena_dev_init's error ladder: does a failing probe release what it took?
#
# F25: it does not on one path.  ena_set_queues_placement_policy failing does
# a bare `return rc` (ena_ethdev.c:2365) instead of goto err_device_destroy,
# which is the only exit between the first acquisition (:2341) and the ladder
# (:2452) that bypasses it.  Reproduces by design, so EXPECTED-FAIL.
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/probe}; mkdir -p "$OUT"
F="--max-field-sensitivity-array-size 256 --unwind 40 \
  --unwindset vsnprintf.1:300 $SOLVER --slice-formula --unwinding-assertions"
for V in 0 1; do
  vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
  rm -f "$OUT/p$V.gb"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DPROBE $vd \
      "$HERE/harness_leaks.c" "$HERE/rdtsc_stub.c" -o "$OUT/p$V.gb" \
      2>&1 | grep -i " error" | head -3 )
  [ -f "$OUT/p$V.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
done
echo "CBMC-CMD: cbmc $OUT/p0.gb --bounds-check $F" > "$OUT/p0.log"
( ulimit -v ${MEM:-90000000}; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/p0.gb" \
    --bounds-check $F ) >> "$OUT/p0.log" 2>&1
sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/p0.log" | grep -E 'PR_|^   VERIFICATION' | sort -u
printf '   other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
  "$(grep -E ': FAILURE$' "$OUT/p0.log" | grep -vcE 'PR_|witness')" \
  "$(grep -cE 'no body for callee.*FAILURE' "$OUT/p0.log")" \
  "$(grep -cE ': UNKNOWN$' "$OUT/p0.log")" \
  "$(grep -cE 'unwind.*: FAILURE' "$OUT/p0.log")"
echo "CBMC-CMD: cbmc $OUT/p1.gb --no-standard-checks $F" > "$OUT/pv.log"
( ulimit -v ${MEM:-90000000}; timeout ${TO:-3000} "$BIN/cbmc" "$OUT/p1.gb" \
    --no-standard-checks $F ) >> "$OUT/pv.log" 2>&1
sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/pv.log" | grep -E '\bwitness\b' | sort -u
