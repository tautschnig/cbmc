#!/bin/bash
# Error-path obligations for ena_tx_queue_setup, and the release-after-failure
# NULL dereference (F10).  RELEASE_AFTER_FAILURE=0 proves the three frame
# obligations; =1 performs the call DPDK is entitled to make next and the
# NULL dereference is reported.
set -eu
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
# Cap the two spin loops of DPDK's x86 rte_spinlock_lock asm.  Under the
# asm-semantics build symex cannot decide them and unwinds both to --unwind
# (cbmc-limitations/asm_spinlock_unwinds.md).  Sound with unwinding
# assertions on (the CBMC 6 default): a lock that could spin fails one.
# SPIN_CAP= disables it.
SPIN_CAP=${SPIN_CAP-"--unwindset rte_spinlock_lock.0:2,rte_spinlock_lock.1:2"}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof}
# flags.txt is written once by run.sh into /tmp/ena-proof; it is NOT per-run
# output, so it must not follow $OUT.  Tying it to $OUT made this runner fail
# instantly under run_all.sh, which gives every runner a fresh OUT.
FLAGS_FILE=${FLAGS_FILE:-/tmp/ena-proof/flags.txt}
RING=${RING:-8}
mkdir -p "$OUT"
[ -f "$FLAGS_FILE" ] || { echo "run run.sh first (it writes flags.txt)"; exit 1; }
cd "$DPDK/build-ql"

RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
         ena_tx_map_mbuf ena_xstats_get ena_xstats_get_names ena_stats_get \
         __rte_raw_cksum ena_rss_configure eth_ena_pci_probe eth_ena_pci_remove; do
  RM="$RM --remove-function-body $f"
done

# Three runs: the proof (RELEASE_AFTER_FAILURE=0), its vacuity witness
# (must FAIL), and the F10 demonstration (RELEASE_AFTER_FAILURE=1, must FAIL).
# The demonstration checks its own outcome and says UNEXPECTED if it stops
# failing; its failure lines are tagged [demonstration] so the sweep
# adjudicator does not count them against the proof.
run_ep() { # R V tag
  local R=$1 V=$2 T=$3 vd=""
  [ "$V" = 1 ] && vd=-DVACUITY_CHECK
  rm -f "$OUT/ep$T.gb" "$OUT/ep$T.log"
  ( ulimit -v 32000000
    "$BIN/goto-cc" $(cat "$FLAGS_FILE") -DPEDANTIC -DCONCRETE_RING=$RING \
      -DRELEASE_AFTER_FAILURE=$R $vd \
      "$HERE/harness_setup_errorpath.c" "$HERE/rdtsc_stub.c" -o "$OUT/ep$T.gb" )
  [ -f "$OUT/ep$T.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/ep$T.gb" "$OUT/ep${T}_rm.gb" >/dev/null 2>&1 )
  ( ulimit -v 60000000
    timeout 2400 "$BIN/cbmc" $SPIN_CAP "$OUT/ep${T}_rm.gb" --bounds-check --pointer-check \
      --unwind $((RING + 4)) --unwinding-assertions > "$OUT/ep$T.log" 2>&1 ) || true
  grep -qE '^VERIFICATION' "$OUT/ep$T.log" ||
    { echo "  NO VERDICT: cbmc did not finish"; tail -3 "$OUT/ep$T.log" | sed 's/^/    /'; }
}

echo "===== RELEASE_AFTER_FAILURE=0: proof ====="
run_ep 0 0 0
grep -E '^VERIFICATION|^\*\* [0-9]' "$OUT/ep0.log" | sed 's/^/  /'
grep -E 'ERR_' "$OUT/ep0.log" | sed -E 's/\] line [0-9]+ /] /' | sed 's/^/  /'
echo "  other FAILURE: $(grep -E ': FAILURE$' "$OUT/ep0.log" | grep -vc 'ERR_')"
echo "  unwind FAIL: $(grep -cE 'unwind.*: FAILURE$' "$OUT/ep0.log")"
echo "  UNKNOWN: $(grep -c ': UNKNOWN$' "$OUT/ep0.log")"

echo "===== RELEASE_AFTER_FAILURE=0: vacuity witness (must FAIL) ====="
run_ep 0 1 0v
grep -E 'witness' "$OUT/ep0v.log" | sed -E 's/\] line [0-9]+ /] /' | sed 's/^/  /'

echo "===== RELEASE_AFTER_FAILURE=1: F10 demonstration, must FAIL ====="
run_ep 1 0 1
n=$(grep -c 'ena_tx_queue_release.*NULL.*: FAILURE$' "$OUT/ep1.log" || true)
if [ "$n" -gt 0 ]; then
  echo "  demonstration reproduces F10: $n NULL dereference(s) in ena_tx_queue_release"
else
  echo "  UNEXPECTED: the F10 demonstration did not fail"
fi
grep 'ena_tx_queue_release.*NULL.*: FAILURE$' "$OUT/ep1.log" \
  | sed -E 's/\] line [0-9]+ /] /; s/: FAILURE$/: FAILURE [demonstration]/' \
  | sed 's/^/    /' | head -3
