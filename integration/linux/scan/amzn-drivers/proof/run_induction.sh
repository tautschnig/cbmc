#!/bin/bash
# The whole induction for the DPDK ENA TX queue, at one ring size, with one
# binary and one set of flags.
#
#   base 1   ena_tx_queue_setup establishes INV_CONFIGURED   harness_setup_basecase.c
#   base 2   ena_queue_start    establishes INV_STARTED+     harness_start_basecase.c
#   step 1   eth_ena_xmit_pkts  preserves   INV_STARTED+     harness_tx_callees.c
#   step 2   ena_tx_cleanup     preserves   INV_STARTED+     harness_cleanup.c
#
# INV_STARTED+ is INV_STARTED, INV_SLOT, OWN_UNIQUE, INV_DESC_SUM and
# INV_FREE_LIST: everything either step assumes.  The runner checks that each
# of those is asserted, and proven, by the base case and by both steps -- the
# composition check -- and that every harness's vacuity witnesses fail.
#
# Named assumptions are compiled in and listed at the end.  The induction holds
# relative to them and to nothing else.
set -u
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-induction}
RING=${RING:-4}
SEGS=${SEGS:-1}
BURST=${BURST:-1}
TO=${TO:-2400}
mkdir -p "$OUT"
FLAGS=${FLAGS:-/tmp/ena-proof/flags.txt}
[ -f "$FLAGS" ] || { echo "run run.sh first (it writes flags.txt)"; exit 1; }
cd "$DPDK/build-ql" || exit 1

COMMON="--sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula"
CHECKS="--bounds-check --pointer-check --unwinding-assertions"
cc()   { ( ulimit -v 32000000; "$BIN/goto-cc" $(cat "$FLAGS") -DPEDANTIC "$@" ); }
inst() { ( ulimit -v 32000000; "$BIN/goto-instrument" "$@" >/dev/null 2>&1 ); }
run()  { local log=$1; shift
         rm -f "$log"
         ( ulimit -v 150000000; timeout "$TO" "$BIN/cbmc" "$@" $COMMON > "$log" 2>&1 )
         grep -qE '^VERIFICATION' "$log" || echo "NO VERDICT" >> "$log"; }

# Removed bodies, by harness.  Anything removed that is actually reached is
# reported as "no body for callee" and counted below.
RM_DATA=""; for f in eth_ena_prep_pkts eth_ena_recv_pkts ena_xstats_get \
  ena_xstats_get_names ena_xstats_get_by_id ena_xstats_get_names_by_id \
  ena_stats_get __rte_raw_cksum ena_rss_configure eth_ena_pci_probe \
  eth_ena_pci_remove ena_interrupt_handler_rte ena_timer_wd_callback; do
  RM_DATA="$RM_DATA --remove-function-body $f"; done
RM_RX=""; for f in ena_populate_rx_queue rte_pktmbuf_alloc_bulk \
  rte_mempool_do_generic_get rte_mempool_ops_dequeue_bulk rte_mempool_get_bulk \
  ena_com_add_single_rx_desc; do RM_RX="$RM_RX --remove-function-body $f"; done
RM_MBUF=""; for f in rte_pktmbuf_free rte_pktmbuf_free_seg rte_pktmbuf_prefree_seg \
  rte_pktmbuf_alloc_bulk rte_pktmbuf_detach rte_mempool_put rte_mempool_put_bulk \
  rte_mempool_get_bulk rte_mempool_generic_put rte_mempool_generic_get \
  rte_mempool_do_generic_put rte_mempool_do_generic_get rte_mempool_cache_flush \
  rte_mbuf_raw_free; do RM_MBUF="$RM_MBUF --remove-function-body $f"; done

echo "ENA TX induction: ring $RING, burst <= $BURST, segments <= $SEGS"
echo "cbmc: $("$BIN/cbmc" --version)   flags: $COMMON"

# ---- base 1: setup ----------------------------------------------------------
for V in 0 1; do
  cc -DCONCRETE_RING=$RING -DVACUITY_CHECK=$V "$HERE/harness_setup_basecase.c" -o "$OUT/b1_$V.gb"
done
run "$OUT/b1.log"     "$OUT/b1_0.gb" $CHECKS --unwind $((RING + 4))
run "$OUT/b1_vac.log" "$OUT/b1_1.gb" --unwind $((RING + 4))

# ---- base 2: queue start ----------------------------------------------------
for V in 0 1; do
  cc -DCONCRETE_RING=$RING -DASSUME_DEVICE_HONEST_DOORBELL -DVACUITY_CHECK=$V \
     "$HERE/harness_start_basecase.c" -o "$OUT/b2_$V.gb"
  inst $RM_DATA $RM_RX --remove-function-body eth_ena_xmit_pkts \
       --remove-function-body ena_xmit_mbuf --remove-function-body ena_tx_map_mbuf \
       --remove-function-body check_for_tx_completion_in_queue \
       "$OUT/b2_$V.gb" "$OUT/b2_${V}_rm.gb"
done
run "$OUT/b2.log"     "$OUT/b2_0_rm.gb" $CHECKS --unwind $((RING + 4))
run "$OUT/b2_vac.log" "$OUT/b2_1_rm.gb" --unwind $((RING + 4))

# ---- base 2, its two callee models checked against the real ena_com code ---
for V in 0 1; do
  cc -DRING=$RING -DASSUME_DEVICE_HONEST_DOORBELL -DVACUITY_CHECK=$V \
     "$HERE/harness_ena_com_create.c" -o "$OUT/m_$V.gb"
  inst --replace-calls ena_com_execute_admin_command:model_execute_admin_command \
       "$OUT/m_$V.gb" "$OUT/m_${V}_r.gb"
done
run "$OUT/m.log"     "$OUT/m_0_r.gb" $CHECKS --unwind 10
run "$OUT/m_vac.log" "$OUT/m_1_r.gb" --unwind 10

# ---- step 1: xmit -----------------------------------------------------------
for V in 0 1; do
  cc -DMAX_BURST=$BURST -DMAX_SEGS=$SEGS -DRING_SIZE=$RING -DCHECK_DESC_SUM=1 \
     -DASSUME_PREPARE_TX_OK -DVACUITY_CHECK=$V \
     "$HERE/harness_tx_callees.c" "$HERE/rdtsc_stub.c" "$HERE/pktmbuf_read_stub.c" \
     -o "$OUT/s1_$V.gb"
  inst $RM_MBUF "$OUT/s1_$V.gb" "$OUT/s1_${V}_rm.gb"
done
run "$OUT/s1.log"     "$OUT/s1_0_rm.gb" $CHECKS --unwind $((RING + 6))
run "$OUT/s1_vac.log" "$OUT/s1_1_rm.gb" --no-standard-checks --unwind $((RING + 6))

# ---- step 2: cleanup --------------------------------------------------------
for V in 0 1; do
  cc -DRING_SIZE=$RING -DMBUF_POOL=$RING -DVACUITY_CHECK=$V \
     "$HERE/harness_cleanup.c" "$HERE/rdtsc_stub.c" -o "$OUT/s2_$V.gb"
  inst $RM_DATA --remove-function-body eth_ena_xmit_pkts \
       --remove-function-body ena_xmit_mbuf --remove-function-body ena_tx_map_mbuf \
       "$OUT/s2_$V.gb" "$OUT/s2_${V}_rm.gb"
done
run "$OUT/s2.log"     "$OUT/s2_0_rm.gb" $CHECKS --unwind $((RING + 4))
run "$OUT/s2_vac.log" "$OUT/s2_1_rm.gb" --unwind $((RING + 4))

# ---- report -----------------------------------------------------------------
status() { grep -E "$2" "$1" | grep -oE '(SUCCESS|FAILURE|UNKNOWN)$' | sort -u | paste -sd/; }
verdict() { grep -oE '^VERIFICATION [A-Z]+|^NO VERDICT' "$1" | head -1; }
echo
printf '%-9s %-24s %s\n' part verdict "failures besides known no-body notices"
for p in b1 b2 m s1 s2; do
  other=$(grep -E ': FAILURE$' "$OUT/$p.log" | grep -vcE 'rte_log_register_type_and_pick_level|rte_pci_register')
  printf '%-9s %-24s %s\n' $p "$(verdict "$OUT/$p.log")" "$other"
done

echo
echo "composition check -- each predicate a step assumes, as proven by each part:"
printf '  %-14s %-10s %-10s %-10s\n' predicate "base(b2)" "xmit(s1)" "clean(s2)"
fail=0
for pred in INV_STARTED INV_SLOT OWN_UNIQUE INV_DESC_SUM INV_FREE_LIST; do
  a=$(status "$OUT/b2.log" "base case: .*$pred")
  b=$(status "$OUT/s1.log" "$pred[: ]")
  c=$(status "$OUT/s2.log" "$pred[: ]")
  printf '  %-14s %-10s %-10s %-10s\n' $pred "${a:-MISSING}" "${b:-MISSING}" "${c:-MISSING}"
  for x in "$a" "$b" "$c"; do [ "$x" = SUCCESS ] || fail=1; done
done
echo "  setup -> start: INV_CONFIGURED $(status "$OUT/b1.log" 'establishes INV_CONFIGURED')"
[ "$(status "$OUT/b1.log" 'establishes INV_CONFIGURED')" = SUCCESS ] || fail=1
m=$(status "$OUT/m.log" 'model|INV_COM holds')
echo "  start's ena_com models vs the real ena_com: ${m:-MISSING}"
[ "$m" = SUCCESS ] || fail=1

echo
echo "step-specific obligations:"
grep -E 'main\.assertion|FREE_ONCE' "$OUT/s1.log" "$OUT/s2.log" | grep -E 'API|INV_wakeup|OWN_TRANSFER|OWN_NO_DANGLING|FREE_ONCE' \
  | sed -E 's|^.*/(s[12])\.log:|  \1 |; s/\] line [0-9]+ /] /'
grep -E 'error path' "$OUT/b2.log" | sed -E 's/\] line [0-9]+ /] /; s/^/  b2 /'

echo
echo "vacuity witnesses (all must be FAILURE):"
for p in b1 b2 m s1 s2; do
  grep -E 'vacuity witness' "$OUT/${p}_vac.log" | sed -E "s/\] line [0-9]+ /] /; s/^/  $p /"
done
vac_bad=$(cat "$OUT"/b1_vac.log "$OUT"/b2_vac.log "$OUT"/m_vac.log "$OUT"/s1_vac.log "$OUT"/s2_vac.log | grep 'vacuity witness' | grep -c ': SUCCESS$')
[ "$vac_bad" = 0 ] || fail=1

echo
echo "the induction holds relative to these assumptions, and only these:"
cat <<'EOF'
  device   ASSUME_DEVICE_HONEST_DOORBELL  doorbell offset inside the BAR (F8 is that the driver does not check)
  device   INV_DEV_LLQ                    LLQ geometry from ena_com_config_dev_mode at device init, not proven
  device   sgl_size <= ENA_PKT_MAX_BUFS   set by ena_init_rings from a clamped device value, not proven
  device   dma_addr_bits in 32..48        device-init precondition of the ena_com check (F13: DPDK stores the error code)
  driver   ASSUME_PREPARE_TX_OK           ena_com_prepare_tx succeeds; on failure the invariants break (F12)
  app      packets in a burst are not already in flight (finding 2: undetected by the driver)
  app      nb_segs < sgl_size (finding 3: advertised, not re-checked)
  harness  tx_free_thresh == 0, offloads == 0, LLQ mode; burst and segment bounds as printed above
EOF
echo
[ $fail = 0 ] && echo "INDUCTION: CLOSED" || echo "INDUCTION: NOT CLOSED"
