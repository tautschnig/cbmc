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

# Three runs: the proof (SEGS_LEGAL=1), its vacuity witnesses (must FAIL),
# and the demonstration that the precondition is needed (SEGS_LEGAL=0, must
# FAIL).  The demonstration checks its own outcome and says UNEXPECTED if it
# stops failing; its failure lines are tagged [demonstration] so the sweep
# adjudicator does not count them against the proof.
run_ms() { # L V tag
  local L=$1 V=$2 T=$3 vd=""
  [ "$V" = 1 ] && vd=-DVACUITY_CHECK
  cd "$DPDK/build-ql"
  rm -f "$OUT/ms$T.gb"
  ( ulimit -v 32000000
    "$BIN/goto-cc" $(cat "$FLAGS_FILE") -DPEDANTIC \
      -DMAX_SEGS=$SEGS -DSEGS_LEGAL=$L $vd \
      "$HERE/harness_map_segs.c" -o "$OUT/ms$T.gb" )
  [ -f "$OUT/ms$T.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  cd "$OUT"
  ( ulimit -v 32000000
    "$BIN/goto-instrument" --remove-function-body rte_pktmbuf_read \
      "ms$T.gb" "ms${T}_rm.gb" >/dev/null 2>&1 )
  ( ulimit -v 48000000
    timeout 900 "$BIN/cbmc" "ms${T}_rm.gb" --bounds-check --pointer-check \
      --unwind $((SEGS + 4)) --unwinding-assertions > "ms$T.log" 2>&1 ) || true
  grep -qE '^VERIFICATION' "ms$T.log" ||
    { echo "  NO VERDICT: cbmc did not finish"; tail -3 "ms$T.log" | sed 's/^/    /'; }
}

echo "== SEGS_LEGAL=1 (chain <= sgl_size): proof"
run_ms 1 0 1
grep -E '^VERIFICATION|^\*\* [0-9]' "$OUT/ms1.log" | sed 's/^/  /'
grep -E 'main\.assertion' "$OUT/ms1.log" | sed 's/^/  /'
# Bodyless callees that may be reached: the RTE_INIT constructors
# (rte_log_register*, rte_pci_register), which CBMC runs before main, and
# rte_pktmbuf_read, removed above.  It only copies a split push header into
# the intermediate buffer, its return value is ignored, and the header bytes
# play no part in the descriptor count.
NOBODY_OK='rte_log_register|rte_pci_register|rte_pktmbuf_read'
echo "  other FAILURE: $(grep -E ': FAILURE$' "$OUT/ms1.log" | grep -v 'no body for callee' | grep -vc 'main\.assertion')"
echo "  no-body reached: $(grep -E 'no body for callee.*FAILURE$' "$OUT/ms1.log" | grep -vcE "$NOBODY_OK")"
echo "  unwind FAIL: $(grep -cE 'unwind.*: FAILURE$' "$OUT/ms1.log")"
echo "  UNKNOWN: $(grep -c ': UNKNOWN$' "$OUT/ms1.log")"

echo "== SEGS_LEGAL=1 vacuity witnesses (must FAIL)"
run_ms 1 1 1v
grep -E 'witness' "$OUT/ms1v.log" | sed 's/^/  /'

echo "== SEGS_LEGAL=0 (chain > sgl_size): demonstration, must FAIL"
run_ms 0 0 0
if grep -q '^VERIFICATION FAILED' "$OUT/ms0.log"; then
  echo "  demonstration reproduces the out-of-bounds write"
else
  echo "  UNEXPECTED: the demonstration did not fail"
fi
grep ': FAILURE$' "$OUT/ms0.log" | grep -v 'no body for callee' \
  | sed -E 's/\].*in /] /; s/: FAILURE$/: FAILURE [demonstration]/' \
  | sort -u | sed 's/^/  /' | head -3
