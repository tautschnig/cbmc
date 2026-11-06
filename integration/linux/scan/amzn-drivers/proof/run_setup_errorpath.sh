#!/bin/bash
# Error-path obligations for ena_tx_queue_setup, and the release-after-failure
# NULL dereference (F10).  RELEASE_AFTER_FAILURE=0 proves the three frame
# obligations; =1 performs the call DPDK is entitled to make next and the
# NULL dereference is reported.
set -eu
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof}
RING=${RING:-8}
mkdir -p "$OUT"
[ -f "$OUT/flags.txt" ] || { echo "run run.sh first (it writes flags.txt)"; exit 1; }
cd "$DPDK/build-ql"

RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
         ena_tx_map_mbuf ena_xstats_get ena_xstats_get_names ena_stats_get \
         __rte_raw_cksum ena_rss_configure eth_ena_pci_probe eth_ena_pci_remove; do
  RM="$RM --remove-function-body $f"
done

for R in 0 1; do
  ( ulimit -v 32000000
    "$BIN/goto-cc" $(cat "$OUT/flags.txt") -DPEDANTIC -DCONCRETE_RING=$RING \
      -DRELEASE_AFTER_FAILURE=$R \
      "$HERE/harness_setup_errorpath.c" "$HERE/rdtsc_stub.c" -o "$OUT/ep$R.gb" )
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/ep$R.gb" "$OUT/ep${R}_rm.gb" >/dev/null 2>&1 )
  rm -f "$OUT/ep$R.log"
  ( ulimit -v 60000000
    timeout 2400 "$BIN/cbmc" "$OUT/ep${R}_rm.gb" --bounds-check --pointer-check \
      --unwind $((RING + 4)) --unwinding-assertions > "$OUT/ep$R.log" 2>&1 ) || true
  echo "===== RELEASE_AFTER_FAILURE=$R ====="
  if ! grep -qE '^VERIFICATION' "$OUT/ep$R.log"; then
    echo "  NO VERDICT: cbmc did not finish"; tail -3 "$OUT/ep$R.log" | sed 's/^/    /'; continue
  fi
  grep -E '^VERIFICATION|^\*\* [0-9]' "$OUT/ep$R.log" | sed 's/^/  /'
  grep -E 'ERR_' "$OUT/ep$R.log" | sed -E 's/\] line [0-9]+ /] /' | sed 's/^/  /'
  echo "  failures in ena_tx_queue_release: $(grep -c 'ena_tx_queue_release.*: FAILURE' "$OUT/ep$R.log")"
  grep ': FAILURE' "$OUT/ep$R.log" | grep -i NULL | sed -E 's/\] line [0-9]+ /] /' | sed 's/^/    /' | head -3
done
