#!/bin/bash
# Re-run the CBMC slices for the amzn-drivers findings.  Each is expected to
# report VERIFICATION FAILED (the out-of-bounds access is reachable).
set -u
cd "$(dirname "$0")"
CBMC=${CBMC:-$(cd ../../../../.. && pwd)/build/bin/cbmc}
run() { f=$1; shift
  r=$( (ulimit -v 8000000; timeout 120 "$CBMC" "$f" --bounds-check --pointer-check "$@" 2>&1) | grep -E '^VERIFICATION')
  echo "$f: $r"; }
run f1_efa_db_bar_idx.c
run f2_ena_rx_frags.c --unwind 20
run f3_ena_hash_key.c
run f4_efa_kverbs_wrid.c
run f5_efa_kverbs_sub_cq_idx.c
run f6_ena_rx_len.c
# Velocity Labs report 1 (DPDK ENA TX prefetch): independent check
run g1_dpdk_tx_prefetch.c --unwind 10
