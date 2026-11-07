#!/bin/bash
# FreeBSD ena_tx_cleanup under a loop contract: unbounded in the number of
# completions processed, with `budget` as the ranking function.  Needs the
# build of README-fbsd-build.md in $FB and a goto-instrument carrying the
# fbsd-dfcc-findings fixes.
set -u
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
FB=${FB:-/tmp/fbsd-build}
HERE=$(cd "$(dirname "$0")" && pwd)
RING=${RING:-4}
OUT=$(mktemp -d)
cd "$FB" || exit 1

for V in 0 1; do
  ( ulimit -v 32000000
    "$BIN/goto-cc" -c $(cat flags.txt) -DRING_SIZE=$RING -DVACUITY_CHECK=$V \
      -DASSUME_DEVICE_REQ_ID_IN_RANGE "$HERE/harness_fbsd_cleanup.c" \
      -o "$OUT/h$V.o" ) || exit 1
  ( ulimit -v 32000000
    "$BIN/goto-cc" "$OUT/h$V.o" ena.o ena_com.o ena_eth_com.o -o "$OUT/g$V.gb" ) 2>/dev/null
  ( ulimit -v 32000000
    "$BIN/goto-instrument" --no-malloc-may-fail \
      --loop-contracts-file "$HERE/fbsd_cleanup_loop_contract.json" \
      --dfcc main --apply-loop-contracts "$OUT/g$V.gb" "$OUT/lc$V.gb" ) >/dev/null 2>&1
done

( ulimit -v 200000000
  timeout 2400 "$BIN/cbmc" "$OUT/lc0.gb" --unwind $((RING + 1)) \
    --unwinding-assertions --sat-solver cadical > "$OUT/proof.log" 2>&1 )
if ! grep -qE '^VERIFICATION' "$OUT/proof.log"; then
  echo "NO VERDICT: cbmc did not finish"; tail -3 "$OUT/proof.log"; exit 1
fi
grep -E '^VERIFICATION|^\*\* [0-9]' "$OUT/proof.log"
echo "obligations:"
grep -E 'FREE_ONCE|DMA_UNMAPPED|INV_NTC|OWN_NO_DANGLING' "$OUT/proof.log" | sed 's/^/  /'
echo "loop contract: $(grep -cE 'loop_invariant|loop_assigns|loop_decreases|loop_step_unwinding' "$OUT/proof.log") properties, \
$(grep -cE '(loop_invariant|loop_assigns|loop_decreases|loop_step_unwinding).*SUCCESS' "$OUT/proof.log") SUCCESS"
echo "UNKNOWN $(grep -c ': UNKNOWN' "$OUT/proof.log")  UNREACHABLE $(grep -c ': UNREACHABLE' "$OUT/proof.log")"

( ulimit -v 200000000
  timeout 2400 "$BIN/cbmc" "$OUT/lc1.gb" --unwind $((RING + 1)) \
    --sat-solver cadical > "$OUT/vac.log" 2>&1 )
echo "vacuity witness (must be FAILURE):"
grep -E 'vacuity witness' "$OUT/vac.log" | sed 's/^/  /'
