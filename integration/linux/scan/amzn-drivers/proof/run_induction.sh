#!/bin/bash
# The whole induction for the DPDK ENA TX queue, at one ring size, with one
# binary and one set of flags.
#
#   d        device init        establishes the LLQ/sgl facts   harness_llq_path.c -DDEVINIT_ONLY
#   base 1   ena_tx_queue_setup establishes INV_STARTABLE    harness_setup_basecase.c
#   base 2   ena_queue_start    INV_STARTABLE -> INV_STARTED+ harness_start_basecase.c
#   m        start's two ena_com models vs the real ena_com   harness_ena_com_create.c
#   step 1   eth_ena_xmit_pkts  preserves   INV_STARTED+     harness_tx_callees.c
#   p        step 1's ena_com_prepare_tx stub vs the real code harness_prepare_tx.c
#   step 2   ena_tx_cleanup     preserves   INV_STARTED+     harness_cleanup.c
#   step 3   ena_queue_stop     INV_STARTED+ -> INV_STARTABLE harness_stop.c
#
# Step 3 closes the lifecycle loop: a stopped (or reset) queue is startable
# again, so the induction covers any sequence of setup, start, xmit, cleanup,
# stop, start, ... on one queue.
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

# ---- d: device init (the LLQ geometry and sgl_size facts INV_DEV_LLQ uses) --
RM_LLQ=$(cat "$HERE/llq_path_rm.txt")
for V in 0 1; do
  cc -DVACUITY_CHECK=$V -DRING=$RING -DBURST=1 -DSEGS=1 -DDEVINIT_ONLY \
     "$HERE/harness_llq_path.c" "$HERE/rdtsc_stub.c" "$HERE/pktmbuf_read_stub.c" \
     -o "$OUT/d_$V.gb"
  inst $RM_LLQ --replace-calls ena_com_execute_admin_command:model_execute_admin_command \
       --replace-calls ena_mem_alloc_coherent:model_mem_alloc_coherent \
       "$OUT/d_$V.gb" "$OUT/d_${V}_r.gb"
done
LLQU="--unwind 10 --unwindset eth_ena_xmit_pkts.0:3,ena_tx_map_mbuf.0:3,memcmp.0:9"
run "$OUT/d.log"     "$OUT/d_0_r.gb" $CHECKS $LLQU
run "$OUT/d_vac.log" "$OUT/d_1_r.gb" $LLQU

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
     -DASSUME_PREPARE_TX_OK -DASSUME_NONEMPTY_PKT -DDISTINCT_MBUFS=1 -DVACUITY_CHECK=$V \
     "$HERE/harness_tx_callees.c" "$HERE/rdtsc_stub.c" "$HERE/pktmbuf_read_stub.c" \
     -o "$OUT/s1_$V.gb"
  inst $RM_MBUF "$OUT/s1_$V.gb" "$OUT/s1_${V}_rm.gb"
done
S1U="--unwind $((RING + 6)) --unwindset eth_ena_xmit_pkts.0:$((BURST + 2)),ena_tx_map_mbuf.0:$((SEGS + 2)),memcmp.0:9"
run "$OUT/s1.log"     "$OUT/s1_0_rm.gb" $CHECKS $S1U
run "$OUT/s1_vac.log" "$OUT/s1_1_rm.gb" --no-standard-checks $S1U

# ---- p: the ena_com_prepare_tx contract step 1 stubs, vs the real code -----
for V in 0 1; do
  VD=""; [ $V = 1 ] && VD=-DVACUITY_CHECK
  cc -DASSUME_NO_F14 $VD "$HERE/harness_prepare_tx.c" -o "$OUT/p_$V.gb"
done
run "$OUT/p.log"     "$OUT/p_0.gb" $CHECKS --signed-overflow-check --undefined-shift-check --unwind 20 --unwindset memcmp.0:9
run "$OUT/p_vac.log" "$OUT/p_1.gb" --unwind 20 --unwindset memcmp.0:9

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

# ---- step 3: queue stop -----------------------------------------------------
RM_STOP=""; for f in eth_ena_prep_pkts eth_ena_xmit_pkts eth_ena_recv_pkts \
  ena_xmit_mbuf ena_tx_map_mbuf ena_tx_cleanup ena_xstats_get ena_xstats_get_names \
  ena_xstats_get_by_id ena_xstats_get_names_by_id ena_stats_get __rte_raw_cksum \
  ena_rss_configure eth_ena_pci_probe eth_ena_pci_remove ena_interrupt_handler_rte \
  ena_timer_wd_callback ena_populate_rx_queue ena_rx_queue_release_bufs \
  rte_mempool_cache_flush; do RM_STOP="$RM_STOP --remove-function-body $f"; done
for V in 0 1; do
  cc -DRING_SIZE=$RING -DVACUITY_CHECK=$V "$HERE/harness_stop.c" "$HERE/rdtsc_stub.c" -o "$OUT/s3_$V.gb"
  inst $RM_STOP "$OUT/s3_$V.gb" "$OUT/s3_${V}_rm.gb"
done
run "$OUT/s3.log"     "$OUT/s3_0_rm.gb" $CHECKS --unwind $((RING + 2))
run "$OUT/s3_vac.log" "$OUT/s3_1_rm.gb" --unwind $((RING + 2))
# its loops have constant bound RING, so no unwinding assertion is generated
# at RING + 2; show one unwinding short does fail one
run "$OUT/s3_short.log" "$OUT/s3_0_rm.gb" --unwinding-assertions --unwind $RING

# ---- report -----------------------------------------------------------------
status() { grep -E "$2" "$1" | grep -oE '(SUCCESS|FAILURE|UNKNOWN)$' | sort -u | paste -sd/; }
verdict() { grep -oE '^VERIFICATION [A-Z]+|^NO VERDICT' "$1" | head -1; }
echo
printf '%-9s %-24s %s\n' part verdict "failures besides known no-body notices"
PARTS="d b1 b2 m s1 p s2 s3"
for p in $PARTS; do
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
x1=$(status "$OUT/b1.log" 'establishes INV_STARTABLE')
x3=$(status "$OUT/s3.log" 'establishes INV_STARTABLE')
echo "  INV_STARTABLE (start assumes it): setup(b1) ${x1:-MISSING}  stop(s3) ${x3:-MISSING}"
[ "$x1" = SUCCESS ] && [ "$x3" = SUCCESS ] || fail=1
echo "  stop(s3) assumes INV_STARTED, INV_FREE_LIST and INV_SLOT, proven above by b2, s1, s2"
xd=$(status "$OUT/d.log" 'DEVINIT')
echo "  device init(d) -> INV_DEV_LLQ geometry, sgl_size bound: ${xd:-MISSING}"
[ "$xd" = SUCCESS ] || fail=1
xp=$(status "$OUT/p.log" 'PTX_')
echo "  xmit's prepare_tx stub vs real ena_com_prepare_tx(p): ${xp:-MISSING}"
[ "$xp" = SUCCESS ] || fail=1
grep -qE 'unwinding assertion.*FAILURE' "$OUT/s3_short.log" &&
  echo "  stop(s3): --unwind $RING fails an unwinding assertion, so --unwind $((RING + 2)) is complete" ||
  { echo "  stop(s3): unwinding completeness NOT shown"; fail=1; }
m=$(status "$OUT/m.log" 'model|INV_COM holds')
echo "  start's ena_com models vs the real ena_com: ${m:-MISSING}"
[ "$m" = SUCCESS ] || fail=1

echo
echo "step-specific obligations:"
grep -E 'main\.assertion|FREE_ONCE' "$OUT/s1.log" "$OUT/s2.log" "$OUT/s3.log" \
  | grep -E 'API|INV_wakeup|OWN_|FREE_ONCE|STOP_' \
  | sed -E 's|^.*/(s[123])\.log:|  \1 |; s/\] line [0-9]+ /] /'
grep -E 'error path' "$OUT/b2.log" | sed -E 's/\] line [0-9]+ /] /; s/^/  b2 /'

echo
echo "vacuity witnesses (all must be FAILURE):"
for p in $PARTS; do
  # in d (DEVINIT_ONLY) everything after device init is unreachable on
  # purpose; only its init witness is meaningful
  sel='vacuity witness'; [ $p = d ] && sel='vacuity witness: LLQ device init completes'
  grep -E "$sel" "$OUT/${p}_vac.log" | sed -E "s/\] line [0-9]+ /] /; s/^/  $p /"
done
vac_bad=0; vac_n=0
for p in $PARTS; do
  sel='vacuity witness'; [ $p = d ] && sel='vacuity witness: LLQ device init completes'
  n=$(grep -E "$sel" "$OUT/${p}_vac.log" | wc -l); vac_n=$((vac_n + n))
  [ "$n" -gt 0 ] || { echo "  $p: NO vacuity witness found"; fail=1; }
  b=$(grep -E "$sel" "$OUT/${p}_vac.log" | grep -c ': SUCCESS$'); vac_bad=$((vac_bad + b))
done
echo "  $vac_n witnesses, $vac_bad not FAILURE"
[ "$vac_bad" = 0 ] || fail=1

echo
echo "the induction holds relative to these assumptions, and only these:"
cat <<'EOF'
  device   ASSUME_DEVICE_HONEST_DOORBELL  doorbell offset inside the BAR (F8: the driver does not check)
  device   dma_addr_bits in 32..48        DPDK stores ena_com_get_dma_width's error code (F13)
  device   ASSUME_NO_F14                  no TX-burst limit, or descs_num_before_header >= 2 and a budget
                                          holding one maximal packet (F14: the doorbell check under-counts)
  device   meta caching enabled           a restriction: the device may set DISABLE_META_CACHING, and
                                          the xmit step covers only queues where it did not
  device   sgl_size >= 1                  0 is possible from the device; xmit is unaffected, not covered
  app      packets in a burst are not already in flight (nothing in the driver detects it)
  app      ASSUME_NONEMPTY_PKT            no zero-length mbuf (F15: accepted with no descriptor; the slot
                                          is never completed, and ring_size of them overrun the free list)
  app      nb_segs < sgl_size             advertised, not re-checked
  harness  tx_free_thresh == 0, offloads == 0, LLQ mode; ring, burst and segment bounds as printed above

  discharged since the last version of this list, by parts of this run:
           INV_DEV_LLQ geometry, sgl_size <= ENA_PKT_MAX_BUFS     d (device init)
           ASSUME_PREPARE_TX_OK -> the real ena_com_prepare_tx    p, under ASSUME_NO_F14
EOF
echo
[ $fail = 0 ] && echo "INDUCTION: CLOSED" || echo "INDUCTION: NOT CLOSED"
