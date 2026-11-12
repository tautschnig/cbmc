/*
 * The fast-free branch of ena_tx_cleanup, so far excluded from every cleanup
 * proof (every harness sets offloads == 0).
 *
 * With RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE the loop batches mbufs instead of
 * freeing them one at a time (ena_ethdev.c:3233-3241):
 *
 *     if (fast_free) {
 *             pkts_to_clean[pkt_cnt++] = mbuf;
 *             mbuf_cnt += mbuf->nb_segs;
 *             if (mbuf_cnt >= ENA_CLEANUP_BUF_THRESH) {
 *                     rte_pktmbuf_free_bulk(pkts_to_clean, pkt_cnt);
 *                     mbuf_cnt = 0;
 *                     pkt_cnt = 0;
 *             }
 *     }
 *
 * where `pkts_to_clean` is a local array of ENA_CLEANUP_BUF_THRESH (256)
 * pointers.  Nothing bounds `pkt_cnt` directly.  What keeps the write in
 * bounds is the pair
 *
 *     pkt_cnt <= mbuf_cnt   and   mbuf_cnt < 256
 *
 * at every loop head: the flush resets both as soon as mbuf_cnt reaches 256,
 * and each iteration adds 1 to pkt_cnt and nb_segs to mbuf_cnt.  The first
 * conjunct therefore holds only while **nb_segs >= 1**.
 *
 * This harness checks that step-wise -- one iteration from an arbitrary state
 * satisfying the invariant -- rather than by unwinding 256 iterations, which
 * is what the overflow would otherwise require.
 *
 *   FF_INDEX  the write index is inside pkts_to_clean
 *   FF_INV    one iteration re-establishes pkt_cnt <= mbuf_cnt < 256
 *
 * -DASSUME_WELL_FORMED_MBUF assumes DPDK's contract that nb_segs >= 1.  The
 * difference between the two configurations is the result.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static size_t nd_size(void) { size_t v; return v; }

#define THRESH ENA_CLEANUP_BUF_THRESH

static struct rte_mbuf the_mbuf;
static struct rte_mbuf *pkts_to_clean[THRESH];
static unsigned g_bulk_frees;

int rte_log(uint32_t l, uint32_t t, const char *f, ...)
{
  (void)l;
  (void)t;
  (void)f;
  return 0;
}
int rte_log_register_type_and_pick_level(const char *n, uint32_t l)
{
  (void)n;
  (void)l;
  return 0;
}
void rte_pci_register(struct rte_pci_driver *d) { (void)d; }
void rte_pktmbuf_free_bulk(struct rte_mbuf **m, unsigned int n)
{
  /* the batch handed to the allocator must lie inside the array */
  __CPROVER_assert(
    n <= THRESH, "FF_BULK: the flushed batch is no larger than the array");
  (void)m;
  g_bulk_frees++;
}

int main(void)
{
  /* an arbitrary loop-head state satisfying the invariant that keeps the
   * write in bounds */
  size_t pkt_cnt = nd_size();
  size_t mbuf_cnt = nd_size();
  __CPROVER_assume(pkt_cnt <= mbuf_cnt);
  __CPROVER_assume(mbuf_cnt < THRESH);

  /* the mbuf this iteration completes: one the driver itself stored in a slot
   * during xmit, so its nb_segs is whatever the application supplied */
  the_mbuf.nb_segs = nd_u16();
#ifdef ASSUME_WELL_FORMED_MBUF
  /* DPDK's mbuf contract: a packet has at least one segment */
  __CPROVER_assume(the_mbuf.nb_segs >= 1);
#endif
  struct rte_mbuf *const mbuf = &the_mbuf;
  g_bulk_frees = 0;

  /* ---- the fast-free branch, verbatim from ena_ethdev.c:3233-3241 ---- */
  __CPROVER_assert(
    pkt_cnt < THRESH,
    "FF_INDEX: the write index is inside pkts_to_clean");
  pkts_to_clean[pkt_cnt++] = mbuf;
  mbuf_cnt += mbuf->nb_segs;
  if(mbuf_cnt >= THRESH)
  {
    rte_pktmbuf_free_bulk(pkts_to_clean, pkt_cnt);
    mbuf_cnt = 0;
    pkt_cnt = 0;
  }

  /* ---- the invariant must survive, or a later iteration overflows ---- */
  __CPROVER_assert(
    pkt_cnt <= mbuf_cnt && mbuf_cnt < THRESH,
    "FF_INV: one iteration re-establishes pkt_cnt <= mbuf_cnt < 256");

#ifdef VACUITY_CHECK
  /* must FAIL: a flush is reachable, so FF_BULK is about something */
  __CPROVER_assert(g_bulk_frees == 0, "vacuity witness: a flush can happen");
  __CPROVER_assert(
    pkt_cnt != 0, "vacuity witness: the counters can be reset by a flush");
#endif
  return 0;
}
