#!/bin/bash
# Stage 2: remove the burst bound.  nb_pkts is unconstrained and the TX loop is
# discharged by the invariant in tx_loop_contract.json via DFCC, so CBMC is
# invoked with NO --unwind at all.  The driver source is not annotated.
#
# The fix from doc/missed-findings-analysis-2026-11.md must be applied to the
# scratch DPDK copy first (the shipped code has the out-of-bounds prefetch, so
# the invariant legitimately fails on it).
set -eu
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof}
mkdir -p "$OUT"
[ -f "$OUT/flags.txt" ] || { echo "run run.sh first (it writes flags.txt)"; exit 1; }

cd "$DPDK/build-ql"
( ulimit -v 32000000
  "$BIN/goto-cc" $(cat "$OUT/flags.txt") -DPEDANTIC \
    "$HERE/harness_tx_unbounded.c" -o "$OUT/s2.gb" )
  [ -f "$OUT/s2.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }

RM=""
for f in rte_pktmbuf_free rte_pktmbuf_free_seg rte_pktmbuf_read \
         rte_mempool_put rte_mempool_put_bulk \
         ena_xmit_mbuf ena_tx_cleanup ena_tx_map_mbuf; do
  RM="$RM --remove-function-body $f"
done
( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/s2.gb" "$OUT/s2_rm.gb" >/dev/null 2>&1 )
# --add-library must run BEFORE --dfcc: the contracts library needs free()'s model
( ulimit -v 32000000; "$BIN/goto-instrument" --add-library "$OUT/s2_rm.gb" "$OUT/s2_lib.gb" >/dev/null 2>&1 )
( ulimit -v 32000000
  "$BIN/goto-instrument" --no-malloc-may-fail \
    --loop-contracts-file "$HERE/tx_loop_contract.json" \
    --dfcc main --apply-loop-contracts "$OUT/s2_lib.gb" "$OUT/s2_lc.gb" >/dev/null 2>&1 )

( ulimit -v 60000000
  timeout 2400 "$BIN/cbmc" "$OUT/s2_lc.gb" --bounds-check --pointer-check \
    --no-standard-checks > "$OUT/stage2.log" 2>&1 ) || true

echo "loop-contract obligations:"
grep -oE '\[eth_ena_xmit_pkts\.(loop_invariant_base|loop_invariant_step|loop_assigns)\.[0-9]+\][^:]*: [A-Z]+' "$OUT/stage2.log" | sed 's/^/  /' | head -8
echo "properties passed: $(grep -c ': SUCCESS' "$OUT/stage2.log")"
echo "failures:"; grep ': FAILURE' "$OUT/stage2.log" | sed 's/^/  /' || echo "  none"
echo
echo "control -- same program without the loop contract, unwind 4:"
( ulimit -v 48000000
  timeout 900 "$BIN/cbmc" "$OUT/s2_lib.gb" --bounds-check --pointer-check \
    --unwind 4 --unwinding-assertions > "$OUT/control.log" 2>&1 ) || true
grep -E 'unwinding assertion' "$OUT/control.log" | sed 's/^/  /' | head -2
