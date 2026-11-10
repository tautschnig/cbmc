#!/bin/bash
# The cleanup operation of the induction step.  The loop is bounded by the
# ring size, so it is discharged by unwinding with --unwinding-assertions and
# needs no loop contract (hence no exposure to DFCC issues B and C).
set -eu
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof}
RING=${RING:-8}
POOL=${POOL:-$RING}   # one distinct mbuf per slot: pool must match the ring
mkdir -p "$OUT"
[ -f "$OUT/flags.txt" ] || { echo "run run.sh first (it writes flags.txt)"; exit 1; }

cd "$DPDK/build-ql"

# The datapath and statistics entry points are not reachable from cleanup;
# removing them keeps CBMC's function-pointer expansion from dragging them in.
# Any of them actually called would show up as "no body for callee".
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
         ena_tx_map_mbuf ena_xstats_get ena_xstats_get_names ena_stats_get \
         __rte_raw_cksum ena_rss_configure eth_ena_pci_probe eth_ena_pci_remove; do
  RM="$RM --remove-function-body $f"
done

for V in 0 1; do
  [ "$V" = 0 ] && TAG="" || TAG="_vac"
  ( ulimit -v 32000000
    "$BIN/goto-cc" $(cat "$OUT/flags.txt") -DPEDANTIC \
      -DRING_SIZE=$RING -DMBUF_POOL=$POOL -DVACUITY_CHECK=$V \
      "$HERE/harness_cleanup.c" "$HERE/rdtsc_stub.c" -o "$OUT/cl$TAG.gb" )
    [ -f "$OUT/cl$TAG.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/cl$TAG.gb" "$OUT/cl${TAG}_rm.gb" >/dev/null 2>&1 )
done

( ulimit -v 60000000
  timeout 2400 "$BIN/cbmc" "$OUT/cl_rm.gb" --bounds-check --pointer-check \
    --unwind $((RING + 4)) --unwinding-assertions --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula > "$OUT/cleanup.log" 2>&1 ) || true

if ! grep -qE '^VERIFICATION' "$OUT/cleanup.log"; then
  echo "NO VERDICT: cbmc did not finish (timeout or memory cap).  Last lines:"
  tail -3 "$OUT/cleanup.log" | sed 's/^/  /'
  exit 1
fi
grep -E '^VERIFICATION|^\*\* ' "$OUT/cleanup.log"
echo "obligations:"; grep -E 'main\.assertion|FREE_ONCE' "$OUT/cleanup.log" | sed 's/^/  /'
echo "vacuity witness (must be FAILURE):"
( ulimit -v 60000000
  timeout 2400 "$BIN/cbmc" "$OUT/cl_vac_rm.gb" --bounds-check --pointer-check \
    --unwind $((RING + 4)) --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula > "$OUT/cleanup_vac.log" 2>&1 ) || true
grep -E 'vacuity witness' "$OUT/cleanup_vac.log" | sed 's/^/  /' || echo "  (not run)"
echo "unmodelled callees actually reached:"
grep -E 'undefined function should be unreachable.*FAILURE' "$OUT/cleanup.log" | sed 's/^/  /' || echo "  none"
