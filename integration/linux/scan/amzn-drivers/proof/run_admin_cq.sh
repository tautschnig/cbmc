#!/bin/bash
# Async operation 3: the admin completion queue over a device-written ring.
set -u
QD=${QD:-4}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/admincq}; mkdir -p "$OUT"
rm -f "$OUT"/a0.gb "$OUT"/a1.gb
FLAGS="--unwind $((QD + 4)) --unwindset ena_com_handle_admin_completion.0:$((QD + 2)) \
  --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula"
for V in 0 1; do
  D="${DEFS:-}"; [ $V = 1 ] && D="$D -DVACUITY_CHECK"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DQD=$QD $D \
      "$HERE/harness_admin_cq.c" -o "$OUT/a$V.gb" 2>&1 | grep -i error | head )
  [ -f "$OUT/a$V.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
done
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/a0.gb" --bounds-check \
  --pointer-check --div-by-zero-check --unwinding-assertions $FLAGS ) > "$OUT/x.log" 2>&1
grep -qE '^VERIFICATION' "$OUT/x.log" || { echo "NO VERDICT"; tail -3 "$OUT/x.log"; }
sed -E 's/\] line [0-9]+ /] /' "$OUT/x.log" | grep -E 'AC_|^VERIFICATION' | sort -u
echo "other FAILURE: $(grep -E ': FAILURE$' "$OUT/x.log" | grep -vcE 'AC_')"
echo "no-body reached: $(grep -cE 'no body for callee.*FAILURE' "$OUT/x.log")"
echo "UNKNOWN: $(grep -c ': UNKNOWN$' "$OUT/x.log")   ERROR: $(grep -c ': ERROR$' "$OUT/x.log")   unwind FAIL: $(grep -cE 'unwind.*: FAILURE$' "$OUT/x.log")"
( ulimit -v 150000000; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/a1.gb" --no-standard-checks $FLAGS ) > "$OUT/v.log" 2>&1
sed -E 's/\] line [0-9]+ /] /' "$OUT/v.log" | grep 'vacuity witness'
