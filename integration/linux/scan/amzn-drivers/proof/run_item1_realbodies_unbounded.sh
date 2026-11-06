#!/bin/bash
# Item 1 of the revised plan: ONE proof with the real callee bodies AND an
# unbounded TX burst.  All five obligations at once.
#
# Needs the inner mbuf-chain walk bounded: the widened outer-loop contract
# havocs the ring, so the chain ena_tx_map_mbuf walks is unconstrained and
# symex follows it forever without a bound (this was misdiagnosed as a DFCC
# performance problem -- see doc/dfcc-assigns-performance-2026-11.md).  The
# unwinding assertion on that loop is checked, so the bound is complete for
# this harness rather than a cut.
#
# Prerequisites: see run.sh.
set -eu
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof}
EXTRA_CBMC=${EXTRA_CBMC:-}
BURST=${BURST:-1}; SEGS=${SEGS:-2}; RING=${RING:-8}
# ena_xmit_mbuf needs free >= nb_segs + 2 and free <= RING - 1, so a
# SEGS-segment packet is only ever accepted when RING >= SEGS + 3.  Below that
# the proof is vacuous: nothing reaches ena_tx_map_mbuf at all.
if [ "$RING" -lt $((SEGS + 3)) ]; then
  echo "WARNING: RING=$RING < SEGS+3=$((SEGS + 3)); no packet can be accepted," >&2
  echo "         so every obligation about the accept path would be vacuous." >&2
fi
SEGBOUND=$((SEGS + 2))
mkdir -p "$OUT"
[ -f "$OUT/flags.txt" ] || { echo "run run.sh first (it writes flags.txt)"; exit 1; }

cd "$DPDK/build-ql"
RM=""
# rte_pktmbuf_read is deliberately NOT in this list: it is packet-data access
# on the LLQ push path, not mempool machinery.  Removing its body cut that
# sub-path silently (DFCC issue A), so the body is supplied instead by
# pktmbuf_read_stub.c, which forwards to DPDK's own inline definition.
for f in rte_pktmbuf_free rte_pktmbuf_free_seg rte_pktmbuf_prefree_seg \
         rte_pktmbuf_alloc_bulk rte_pktmbuf_detach \
         rte_mempool_put rte_mempool_put_bulk rte_mempool_get_bulk \
         rte_mempool_generic_put rte_mempool_generic_get \
         rte_mempool_do_generic_put rte_mempool_do_generic_get \
         rte_mempool_cache_flush rte_mbuf_raw_free; do
  RM="$RM --remove-function-body $f"
done

# tag "" = the proof; tag "_vac" = the same harness asserting sent == 0, which
# must FAIL for the proof to be about anything.
for V in 0 1; do
  [ "$V" = 0 ] && TAG="" || TAG="_vac"
  ( ulimit -v 32000000
    "$BIN/goto-cc" $(cat "$OUT/flags.txt") -DPEDANTIC \
      -DMAX_BURST=$BURST -DMAX_SEGS=$SEGS -DRING_SIZE=$RING -DVACUITY_CHECK=$V \
      "$HERE/harness_tx_callees.c" "$HERE/rdtsc_stub.c" "$HERE/pktmbuf_read_stub.c" -o "$OUT/i1$TAG.gb" )
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/i1$TAG.gb" "$OUT/i1${TAG}_rm.gb" >/dev/null 2>&1 )
  ( ulimit -v 32000000; "$BIN/goto-instrument" --add-library "$OUT/i1${TAG}_rm.gb" "$OUT/i1${TAG}_lib.gb" >/dev/null 2>&1 )
  ( ulimit -v 32000000
    "$BIN/goto-instrument" --no-malloc-may-fail \
      --loop-contracts-file "$HERE/tx_loop_contract_fields.json" \
      --dfcc main --apply-loop-contracts "$OUT/i1${TAG}_lib.gb" "$OUT/i1${TAG}_lc.gb" >/dev/null 2>&1 )
done

rm -f "$OUT/item1.log" "$OUT/item1_vac.log"
( ulimit -v 60000000
  timeout 2400 "$BIN/cbmc" "$OUT/i1_lc.gb" --bounds-check --pointer-check \
    --no-standard-checks \
    --unwindset ena_tx_map_mbuf.0:$SEGBOUND --unwinding-assertions $EXTRA_CBMC \
    > "$OUT/item1.log" 2>&1 ) || true

if ! grep -qE '^VERIFICATION' "$OUT/item1.log"; then
  echo "NO VERDICT: cbmc did not finish (timeout or memory cap).  Last lines:"
  tail -3 "$OUT/item1.log" 2>/dev/null | sed 's/^/  /'
  exit 1
fi
grep -E '^VERIFICATION|^\*\* ' "$OUT/item1.log"
echo "obligations:"; grep -E 'main\.assertion' "$OUT/item1.log" | sed 's/^/  /'
echo "vacuity witness (must be FAILURE, else the accept path is unreachable):"
( ulimit -v 60000000
  timeout 900 "$BIN/cbmc" "$OUT/i1_vac_lc.gb" --bounds-check --pointer-check \
    --no-standard-checks --unwindset ena_tx_map_mbuf.0:$SEGBOUND \
    > "$OUT/item1_vac.log" 2>&1 ) || true
grep -E 'vacuity witness' "$OUT/item1_vac.log" | sed 's/^/  /' || echo "  (not run)"
echo "loop contract + inner-loop bound:"
grep -E 'ena_tx_map_mbuf\.unwind|loop_invariant_(base|step)\.1|loop_assigns\.1' "$OUT/item1.log" | sed 's/^/  /'
