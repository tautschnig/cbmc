#!/bin/bash
# Usage: [RING=4] [BURST=1] run_xmit_variant.sh NAME [-DDEFINE ...]
#   F15: run_xmit_variant.sh B -DALLOW_EMPTY_IN_FLIGHT; BURST=2 ... for OWN_NO_LOSS
# s1 (xmit step) variants for the empty-packet question
set -u
RING=${RING:-4}; BURST=${BURST:-1}
B=/home/ubuntu/cbmc-github.git/build/bin; P=/home/ubuntu/cbmc-github.git/integration/linux/scan/amzn-drivers/proof
W=${W:-/tmp/ena-proof/s1var}; mkdir -p $W
RM=""; for f in rte_pktmbuf_free rte_pktmbuf_free_seg rte_pktmbuf_prefree_seg rte_pktmbuf_alloc_bulk rte_pktmbuf_detach rte_mempool_put rte_mempool_put_bulk rte_mempool_get_bulk rte_mempool_generic_put rte_mempool_generic_get rte_mempool_do_generic_put rte_mempool_do_generic_get rte_mempool_cache_flush rte_mbuf_raw_free; do RM="$RM --remove-function-body $f"; done
name=$1; shift
( cd /tmp/dpdk/build-ql && ulimit -v 32000000 && $B/goto-cc $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC -DMAX_BURST=$BURST -DMAX_SEGS=1 -DRING_SIZE=$RING -DCHECK_DESC_SUM=1 -DASSUME_PREPARE_TX_OK -DDISTINCT_MBUFS=1 "$@" $P/harness_tx_callees.c $P/rdtsc_stub.c $P/pktmbuf_read_stub.c -o $W/$name.gb ) || exit 1
( ulimit -v 32000000; $B/goto-instrument $RM $W/$name.gb $W/${name}_rm.gb >/dev/null 2>&1 )
U="--unwind $((RING + 6)) --unwindset eth_ena_xmit_pkts.0:$((BURST+2)),ena_tx_map_mbuf.0:3,memcmp.0:9"
case "$*" in *VACUITY_CHECK=1*) CH="--no-standard-checks";; *) CH="--bounds-check --pointer-check --unwinding-assertions";; esac
( ulimit -v 150000000; timeout 3600 $B/cbmc $W/${name}_rm.gb $CH $U --sat-solver cadical --max-field-sensitivity-array-size 256 --slice-formula --trace ) > $W/$name.log 2>&1
echo "== $name: $(grep -E '^VERIFICATION' $W/$name.log)"
sed -E 's/\] line [0-9]+ /] /' $W/$name.log | grep -E ': (SUCCESS|FAILURE)$' | grep -E 'main\.|FREE_ONCE|RELEASE|witness|unwind.*FAIL|no body.*FAIL' | sort -u
