#!/bin/bash
# RX error paths: the F10 shape in ena_rx_queue_release, and the interrupt
# ops on a queue that was never started.
set -u
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/rx-err}; mkdir -p "$OUT"
RING=${RING:-4}
RM=""
for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts ena_xmit_mbuf \
  ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure eth_ena_pci_probe eth_ena_pci_remove ena_populate_rx_queue \
  ena_interrupt_handler_rte ena_timer_wd_callback; do
  RM="$RM --remove-function-body $f"; done
run() {
  local tag=$1; shift
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DRING=$RING "$@" \
      "$HERE/harness_rx_errorpath.c" "$HERE/rdtsc_stub.c" -o "$OUT/$tag.gb" \
      2>&1 | grep -i error | head ) || return 1
  ( ulimit -v 32000000; "$BIN/goto-instrument" $RM "$OUT/$tag.gb" "$OUT/${tag}_rm.gb" >/dev/null 2>&1 )
  ( ulimit -v 60000000; timeout 1200 "$BIN/cbmc" "$OUT/${tag}_rm.gb" --bounds-check \
    --pointer-check --unwind $((RING + 3)) --sat-solver cadical \
    --max-field-sensitivity-array-size 256 --slice-formula ) > "$OUT/$tag.log" 2>&1
  echo "== $tag: $(grep -E '^VERIFICATION' "$OUT/$tag.log")"
  sed -E 's/\] line [0-9]+ /] /; s/^/    /' "$OUT/$tag.log" |
    grep -E 'RX_RELEASE_NONNULL|INTR_ON_UNSTARTED|dereference failure.*(ring|rxq)->.*FAILURE' | sort -u | head -5
}
run frame      -DINTR_ON_UNSTARTED=0 -DRELEASE_AFTER_FAILURE=0
run released   -DINTR_ON_UNSTARTED=0 -DRELEASE_AFTER_FAILURE=1
run intr       -DINTR_ON_UNSTARTED=1
