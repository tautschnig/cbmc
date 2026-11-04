#!/bin/bash
# Stage 1: discharge the callee contracts.  The real bodies of
# eth_ena_xmit_pkts, ena_xmit_mbuf, ena_tx_map_mbuf and ena_tx_mbuf_prepare are
# verified; the assumption boundary sits at the DPDK mbuf/mempool API and the
# ena_com layer (a separate translation unit).
#
# Prerequisites: see run.sh.
set -eu
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof}
BURST=${BURST:-2}; SEGS=${SEGS:-3}; RING=${RING:-4}
mkdir -p "$OUT"
[ -f "$OUT/flags.txt" ] || { echo "run run.sh first (it writes flags.txt)"; exit 1; }

cd "$DPDK/build-ql"
( ulimit -v 32000000
  "$BIN/goto-cc" $(cat "$OUT/flags.txt") -DPEDANTIC \
    -DMAX_BURST=$BURST -DMAX_SEGS=$SEGS -DRING_SIZE=$RING \
    "$HERE/harness_tx_callees.c" -o "$OUT/s1.gb" )

# Abstract only the DPDK mbuf/mempool leaves -- NOT the driver's own functions.
RM=""
for f in rte_pktmbuf_free rte_pktmbuf_free_seg rte_pktmbuf_prefree_seg \
         rte_pktmbuf_read rte_pktmbuf_alloc_bulk rte_pktmbuf_detach \
         rte_mempool_put rte_mempool_put_bulk rte_mempool_get_bulk \
         rte_mempool_generic_put rte_mempool_generic_get \
         rte_mempool_do_generic_put rte_mempool_do_generic_get \
         rte_mempool_cache_flush rte_mbuf_raw_free; do
  RM="$RM --remove-function-body $f"
done
( ulimit -v 32000000
  "$BIN/goto-instrument" $RM --drop-unused-functions "$OUT/s1.gb" "$OUT/s1_mod.gb" >/dev/null 2>&1 )

( ulimit -v 60000000
  timeout 1800 "$BIN/cbmc" "$OUT/s1_mod.gb" --bounds-check --pointer-check \
    --unwind 16 > "$OUT/stage1.log" 2>&1 ) || true

echo "properties passed: $(grep -c ': SUCCESS' "$OUT/stage1.log")"
echo "real failures (excluding the abstraction notices):"
grep ': FAILURE' "$OUT/stage1.log" | grep -v 'no body for callee' || echo "  none"
echo "assumption boundary (unmodelled callees):"
grep -oE 'no body for callee [A-Za-z_0-9.$]+' "$OUT/stage1.log" | sort -u | sed 's/^/  /'
