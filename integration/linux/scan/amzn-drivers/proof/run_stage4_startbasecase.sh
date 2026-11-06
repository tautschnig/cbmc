#!/bin/bash
# Stage 3: the BASE CASE -- ena_tx_queue_setup establishes INV_CONFIGURED.
# Together with the step proofs this closes the induction over operation
# sequences.
#
# No DFCC and no loop contract here: the driver's init loop writes through
# empty_tx_reqs, which lives in an anonymous union, and that defeated both the
# loop-contract route and a malloc-based allocator model (see the design doc,
# "A CBMC limitation worth chasing").  The loop is unrolled instead, which is
# why the ring size is concrete.
set -eu
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof}
RING=${RING:-8}
mkdir -p "$OUT"
[ -f "$OUT/flags.txt" ] || { echo "run run.sh first (it writes flags.txt)"; exit 1; }

cd "$DPDK/build-ql"
( ulimit -v 32000000
  "$BIN/goto-cc" $(cat "$OUT/flags.txt") -DPEDANTIC -DCONCRETE_RING=$RING -DASSUME_DEVICE_HONEST_DOORBELL \
    "$HERE/harness_start_basecase.c" -o "$OUT/s4.gb" )

( ulimit -v 60000000
  timeout 1800 "$BIN/cbmc" "$OUT/s4.gb" --bounds-check --pointer-check \
    --unwind $((RING + 4)) --unwinding-assertions > "$OUT/stage4.log" 2>&1 ) || true

grep -E '^VERIFICATION|^\*\* ' "$OUT/stage4.log"
grep -E 'base case|error path' "$OUT/stage4.log" || true
echo "unmodelled callees:"
grep -oE 'no body for callee [A-Za-z_0-9.$]+' "$OUT/stage4.log" | sort -u | sed 's/^/  /' || echo "  none"
