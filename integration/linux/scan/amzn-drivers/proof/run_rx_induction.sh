#!/bin/bash
# The RX induction for the DPDK ENA driver: every part, one report.
#
#   u   ena_rx_queue_setup        establishes INV_RX_STARTABLE   harness_rx_setup.c
#   t   ena_queue_start (RX)      STARTABLE -> INV_RX_STARTED    harness_rx_start.c
#   c   ena_com_rx_pkt contract vs the real ena_eth_com.c        harness_rx_com.c
#   r   eth_ena_recv_pkts         preserves INV_RX_STARTED       harness_rx_recv.c
#   f   ena_populate_rx_queue     preserves INV_RX_STARTED       harness_rx_refill.c
#   s   ena_rx_queue_release_bufs STARTED -> INV_RX_STARTABLE    harness_rx_stop.c
#
# s closes the lifecycle loop, so the induction covers any sequence of
# setup, start, {recv, refill}*, stop, start, ... on one RX queue.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
RING=${RING:-4}
OUT=${OUT:-/tmp/ena-rx-induction}
mkdir -p "$OUT"
echo "ENA RX induction: ring $RING"
echo "cbmc: $(${B:-/home/ubuntu/cbmc-github.git/build/bin}/cbmc --version)"
echo

run_part() {
  local tag=$1 script=$2; shift 2
  W="$OUT/$tag" "$HERE/$script" "$@" > "$OUT/$tag.txt" 2>&1
  printf '%-4s %-22s %s\n' "$tag" "$script" \
    "$(grep -cE 'SUCCESS$' "$OUT/$tag.txt") obligations SUCCESS"
}
if [ "${REPORT_ONLY:-0}" != 1 ]; then
  RING=$RING run_part u run_rx_setup.sh
  RING=$RING run_part t run_rx_start.sh
  QD=$RING run_part c run_rx_com.sh
  RING=$RING MC=2 run_part r run_rx_recv.sh
  RING=$RING run_part f run_rx_refill.sh
  RING=8 run_part s run_rx_stop.sh
fi

echo
printf '%-4s %-26s %s\n' part verdict "clean"
fail=0
for p in u t c r f s; do
  # run_rx_com.sh prefixes its verdict with the sub-run name, the others do not
  v=$(grep -oE 'VERIFICATION [A-Z]+' "$OUT/$p.txt" | head -1)
  # the vacuity build of each part is expected to FAIL (its witnesses must)
  bad=$(grep -E 'other FAILURE: [1-9]|no-body reached: [1-9]|UNKNOWN: [1-9]|ERROR: [1-9]|unwind FAIL: [1-9]' "$OUT/$p.txt" | wc -l)
  [ "$v" = "VERIFICATION SUCCESSFUL" ] || fail=1
  [ "$bad" = 0 ] || fail=1
  printf '%-4s %-26s %s\n' "$p" "${v:-NO VERDICT}" "$([ "$bad" = 0 ] && echo yes || echo NO)"
done

echo
echo "composition check:"
x=$(grep -h 'establishes INV_RX_STARTABLE' "$OUT/u.txt" | grep -oE 'SUCCESS|FAILURE')
y=$(grep -h 'RXS_RESTARTABLE' "$OUT/s.txt" | grep -oE 'SUCCESS|FAILURE')
echo "  INV_RX_STARTABLE (start assumes it):  setup(u) ${x:-MISSING}   stop(s) ${y:-MISSING}"
[ "$x" = SUCCESS ] && [ "$y" = SUCCESS ] || fail=1
z=$(grep -h 'establishes INV_RX_STARTED' "$OUT/t.txt" | grep -oE 'SUCCESS|FAILURE')
a=$(grep -h 'RX_STARTED:' "$OUT/r.txt" | grep -oE 'SUCCESS|FAILURE')
b=$(grep -h 'RF_STARTED:' "$OUT/f.txt" | grep -oE 'SUCCESS|FAILURE')
echo "  INV_RX_STARTED (steps assume it):     start(t) ${z:-MISSING}   recv(r) ${a:-MISSING}   refill(f) ${b:-MISSING}"
for v in "$z" "$a" "$b"; do [ "$v" = SUCCESS ] || fail=1; done
d=$(grep -h 'RXC_REQID' "$OUT/c.txt" | grep -oE 'SUCCESS|FAILURE' | head -1)
echo "  recv's ena_com_rx_pkt stub vs real:   contract(c) ${d:-MISSING}"
[ "$d" = SUCCESS ] || fail=1

echo
echo "vacuity witnesses (all must be FAILURE):"
vac_bad=0; vac_n=0
for p in u t c r f s; do
  while read -r line; do
    echo "  $p $line"
    vac_n=$((vac_n + 1))
    case "$line" in *": SUCCESS") vac_bad=$((vac_bad + 1));; esac
  done < <(grep -h 'vacuity witness' "$OUT/$p.txt")
done
echo "  $vac_n witnesses, $vac_bad not FAILURE"
[ "$vac_bad" = 0 ] && [ "$vac_n" -gt 0 ] || fail=1

echo
echo "the RX induction holds relative to these assumptions, and only these:"
cat <<'LEDGER'
  device   ASSUME_NO_F16                  the device names a posted slot, and does not repeat a
                                          req_id within one packet's chain (F16: ena_rx_mbuf checks
                                          neither, unlike Linux and FreeBSD)
  device   ASSUME_DEVICE_HONEST_DOORBELL  the CQ unmask/NUMA/doorbell offsets are inside the BAR
                                          (F8/F9: the driver does not check)
  model    ena_com_create_io_queue and ena_com_get_io_handlers are replaced by the post-conditions
           harness_ena_com_create.c verifies against the real ena_com code (as on TX)
  app      the mempool hands out distinct buffers the driver does not already own (DPDK's contract)
  harness  rx_free_thresh forces refill to be a separate step; offloads == 0; host placement;
           ring and chain bounds as printed above
LEDGER
echo
[ $fail = 0 ] && echo "RX INDUCTION: CLOSED" || echo "RX INDUCTION: NOT CLOSED"
