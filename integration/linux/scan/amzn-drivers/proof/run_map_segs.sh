#!/bin/bash
# Focused obligation: is the bound on tx_info->bufs[ENA_PKT_MAX_BUFS] a
# property of the driver, or an obligation on the application?
#
# ena_tx_map_mbuf walks the mbuf ->next chain doing `ena_buf++` per segment
# with no bound of its own.  eth_ena_prep_pkts (the rte_eth_tx_prepare
# callback) checks nb_segs against sgl_size, but DPDK permits calling
# rte_eth_tx_burst without tx_prepare.
#
#   SEGS_LEGAL=1  chain <= sgl_size   -> expect SUCCESS
#   SEGS_LEGAL=0  chain >  sgl_size   -> expect FAILURE (OOB write)
set -eu
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof}
# flags.txt is written once by run.sh into /tmp/ena-proof; it is NOT per-run
# output, so it must not follow $OUT.  Tying it to $OUT made this runner fail
# instantly under run_all.sh, which gives every runner a fresh OUT.
FLAGS_FILE=${FLAGS_FILE:-/tmp/ena-proof/flags.txt}
SEGS=${SEGS:-20}
mkdir -p "$OUT"
[ -f "$FLAGS_FILE" ] || { echo "run run.sh first (it writes flags.txt)"; exit 1; }

for L in 1 0; do
 for V in 0 1; do
  vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
  cd "$DPDK/build-ql"
  ( ulimit -v 32000000
    "$BIN/goto-cc" $(cat "$FLAGS_FILE") -DPEDANTIC \
      -DMAX_SEGS=$SEGS -DSEGS_LEGAL=$L $vd \
      "$HERE/harness_map_segs.c" -o "$OUT/ms$L.gb" )
  cd "$OUT"
  ( ulimit -v 32000000
    "$BIN/goto-instrument" --remove-function-body rte_pktmbuf_read \
      "ms$L.gb" "ms${L}_rm.gb" >/dev/null 2>&1 )
  ( ulimit -v 48000000
    timeout 900 "$BIN/cbmc" "ms${L}_rm.gb" --bounds-check --pointer-check \
      --unwind $((SEGS + 4)) --unwinding-assertions > "ms$L.log" 2>&1 ) || true
  echo "== SEGS_LEGAL=$L$([ $V = 1 ] && echo ' (vacuity witnesses, must FAIL)') (chain $([ "$L" = 1 ] && echo '<=' || echo '>') sgl_size)"
  grep -E 'main\.assertion' "ms$L.log" | sed 's/^/  /'
  grep ': FAILURE' "ms$L.log" | grep -v 'no body for callee' \
    | sed -E 's/\].*in /] /' | sort -u | sed 's/^/  /' | head -3
 done
done
