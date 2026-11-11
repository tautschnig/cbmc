/*
 * The cleanup step without a ring-size bound: a hand-written loop invariant,
 * machine-checked in three parts.
 *
 * ena_tx_cleanup's loop was proven only by unwinding it ring_size times, so
 * the cost grew with the ring and ring 8 did not finish (10.7M clauses,
 * solver-bound; see ena-cleanup-ring8-2026-11.md).  A DFCC loop contract did
 * not help, because the invariant cannot be written as a side-effect-free
 * expression -- INV_CLEAN needs loops over the ring.
 *
 * So the induction over iterations is done by hand, with each part checked
 * separately.  Each part is ONE iteration's worth of work, so none of them
 * grows with the ring size: that is the point.
 *
 *   PART=1  ENTRY.  INV_STARTED and INV_FREE_LIST and INV_DESC_SUM, as the
 *           caller guarantees, imply INV_CLEAN with the loop's initial
 *           cursor (the ring's own next_to_clean) and zero descriptors
 *           collected.  This is what lets the loop start.
 *
 *   PART=2  STEP.  From any state satisfying INV_CLEAN, one iteration of the
 *           loop body re-establishes INV_CLEAN with the cursor advanced by
 *           one and the accumulator increased by the slot's descriptor count.
 *           The body is reproduced from ena_ethdev.c:3218-3253 rather than
 *           called, because it is the inside of a loop and cannot be invoked
 *           on its own; the reproduction is line-for-line and listed in the
 *           comment there.
 *
 *   PART=3  EXIT.  INV_CLEAN, plus the two writes the function performs after
 *           the loop (the cursor write-back and ena_com_comp_ack), gives
 *           INV_FREE_LIST and INV_DESC_SUM again -- the post-condition the
 *           induction needs.
 *
 * Together: the loop preserves INV_CLEAN (2) from a state where it holds (1),
 * and on exit it yields the required post-state (3), for ANY number of
 * iterations and ANY ring size.  What is not machine-checked is the splice
 * itself -- that parts 1, 2 and 3 compose -- and that the reproduced body in
 * part 2 matches the source.  Both are stated here so the gap is visible.
 *
 * Switches: RING, PART, VACUITY_CHECK.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING
#define RING 8
#endif
#ifndef PART
#define PART 2
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_adapter adapter;
static struct ena_ring ring;
static struct ena_tx_buffer pool_tbi[RING];
static uint16_t pool_etr[RING];
static uint8_t pool_push[96];
static struct rte_mbuf pool_mbuf[RING];
static struct ena_com_io_sq the_sq;
static struct ena_com_io_cq the_cq;
static uint32_t the_db;

/* ghost: the watched mbuf, to carry the free discipline through the step */
static struct rte_mbuf *g_watch;
static unsigned g_free_calls;

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
void rte_pktmbuf_free(struct rte_mbuf *m)
{
  if(m == NULL)
    return;
  __CPROVER_assert(
    m != g_watch || g_free_calls == 0,
    "CS_FREE_ONCE: no mbuf is released twice");
  if(m == g_watch)
    g_free_calls++;
}

int main(void)
{
  unsigned i;

  __CPROVER_havoc_object(&the_sq.llq_info);
  ring.adapter = &adapter;
  ring.type = ENA_RING_TYPE_TX;
  ring.id = 0;
  ring.configured = 1;
  ring.ring_size = RING;
  ring.size_mask = RING - 1;
  ring.sgl_size = ENA_PKT_MAX_BUFS;
  ring.tx_buffer_info = pool_tbi;
  ring.empty_tx_reqs = pool_etr;
  ring.push_buf_intermediate_buf = pool_push;
  ring.ena_com_io_sq = &the_sq;
  ring.ena_com_io_cq = &the_cq;
  ring.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  the_sq.mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  the_sq.q_depth = RING;
  the_sq.db_addr = &the_db;
  the_sq.tail = nd_u16();
  the_sq.next_to_comp = nd_u16();
  ring.next_to_use = nd_u16() & (RING - 1);
  ring.next_to_clean = nd_u16() & (RING - 1);
  ring.offloads = 0; /* no fast-free: that path is a separate configuration */

  for(i = 0; i < RING; i++)
  {
    pool_tbi[i].mbuf = nd_int() ? &pool_mbuf[i] : NULL;
    pool_tbi[i].tx_descs = nd_u32();
    pool_tbi[i].num_of_bufs = nd_u16();
    __CPROVER_assume(pool_tbi[i].num_of_bufs <= ENA_PKT_MAX_BUFS);
    pool_etr[i] = nd_u16();
    pool_mbuf[i].nb_segs = 1;
  }
  __CPROVER_assume(INV_COM(&the_sq));
  __CPROVER_assume(INV_STARTED(&ring));
  g_watch = &pool_mbuf[nd_u16() % RING];
  g_free_calls = 0;

#if PART == 1
  /* ---- ENTRY: the caller's guarantee implies INV_CLEAN at the loop head ---- */
  __CPROVER_assume(INV_FREE_LIST_FAST(&ring, RING));
  {
    uint16_t sum = 0;
    for(i = 0; i < RING; i++)
      if(pool_tbi[i].mbuf != NULL)
        sum = (uint16_t)(sum + TX_DESCS(pool_tbi[i]));
    __CPROVER_assume((uint16_t)(the_sq.tail - the_sq.next_to_comp) == sum);
  }
  __CPROVER_assert(
    INV_CLEAN(&ring, RING, ring.next_to_clean, 0),
    "CS_ENTRY: the caller's invariant implies INV_CLEAN at the loop head");

#elif PART == 2
  /* ---- STEP: one loop-body iteration preserves INV_CLEAN ---- */
  uint16_t ntc = nd_u16();
  uint16_t acked = nd_u16();
  __CPROVER_assume(INV_CLEAN(&ring, RING, ntc, acked));

  /* The device supplies the completion.  validate_tx_req_id is the real
   * function and is what bounds req_id and rejects a free slot. */
  const uint16_t req_id = nd_u16();
  if(validate_tx_req_id(&ring, req_id) != 0)
    return 0; /* the loop breaks; nothing to preserve */

  /*
   * The loop body, ena_ethdev.c:3229-3253, with the fast-free branch excluded
   * (offloads == 0 above):
   *
   *   tx_info = &tx_ring->tx_buffer_info[req_id];
   *   tx_info->timestamp = 0;
   *   mbuf = tx_info->mbuf;
   *   rte_pktmbuf_free(mbuf);
   *   tx_info->mbuf = NULL;
   *   tx_ring->empty_tx_reqs[next_to_clean] = req_id;
   *   total_tx_descs += tx_info->tx_descs;
   *   total_tx_pkts++;
   *   next_to_clean = ENA_IDX_NEXT_MASKED(next_to_clean, tx_ring->size_mask);
   */
  struct ena_tx_buffer *tx_info = &ring.tx_buffer_info[req_id];
  const uint16_t descs_here = TX_DESCS(*tx_info);
  tx_info->timestamp = 0;
  struct rte_mbuf *mbuf = tx_info->mbuf;
  rte_pktmbuf_free(mbuf);
  tx_info->mbuf = NULL;
  ring.empty_tx_reqs[ntc] = req_id;
  const uint16_t acked_next = (uint16_t)(acked + descs_here);
  const uint16_t ntc_next = ENA_IDX_NEXT_MASKED(ntc, ring.size_mask);

  __CPROVER_assert(
    INV_CLEAN(&ring, RING, ntc_next, acked_next),
    "CS_STEP: one completion preserves INV_CLEAN, with the cursor advanced "
    "and the descriptors accumulated");

#else
  /* ---- EXIT: INV_CLEAN plus the two post-loop writes gives the originals ---- */
  uint16_t ntc = nd_u16();
  uint16_t acked = nd_u16();
  __CPROVER_assume(INV_CLEAN(&ring, RING, ntc, acked));
  /* ena_ethdev.c:3256-3260, the two writes after the loop */
  if(acked > 0)
  {
    ring.next_to_clean = ntc;
    the_sq.next_to_comp = (uint16_t)(the_sq.next_to_comp + acked);
  }
  __CPROVER_assert(
    acked == 0 || INV_FREE_LIST_FAST(&ring, RING),
    "CS_EXIT_FL: after the write-back INV_FREE_LIST holds again");
  {
    uint16_t sum = 0;
    for(i = 0; i < RING; i++)
      if(pool_tbi[i].mbuf != NULL)
        sum = (uint16_t)(sum + TX_DESCS(pool_tbi[i]));
    __CPROVER_assert(
      acked == 0 ||
        (uint16_t)(the_sq.tail - the_sq.next_to_comp) == sum,
      "CS_EXIT_DS: after the ack INV_DESC_SUM holds again");
  }
  __CPROVER_assert(
    INV_STARTED(&ring), "CS_EXIT_STARTED: INV_STARTED still holds");
#endif

#ifdef VACUITY_CHECK
#  if PART == 2
  /* must FAIL: a completion really is accepted and a packet really is freed */
  __CPROVER_assert(
    g_free_calls == 0, "vacuity witness: the step can free the watched packet");
#  else
  __CPROVER_assert(
    ring.next_to_use == 0xffff, "vacuity witness: the part is reachable");
#  endif
#endif
  return 0;
}
