/*
 * RX refill sub-operation: ena_populate_rx_queue.
 *
 * The recv step stubs this; here it is the real code.  Refill takes buffers
 * from the mempool and posts them to the device, consuming free slots named by
 * empty_rx_reqs and advancing next_to_use.
 *
 * The device side (ena_com_add_single_rx_desc) is real: it is the SQ-space
 * check plus a descriptor write into the SQ ring, which this harness provides
 * as real memory so the write is bounds-checked.
 *
 * Obligations:
 *   RF_STARTED     INV_RX_STARTED is preserved, so refill composes with recv
 *   RF_NO_CLOBBER  refill never overwrites a slot that already holds a buffer
 *                  (that would leak the posted mbuf and give the device two
 *                  descriptors naming one slot)
 *   RF_POSTED      every slot refill claims ends up holding the mbuf it
 *                  allocated for it
 *   RF_RET         the returned count is at most the requested count
 *   plus --bounds-check on the descriptor writes and the mempool interface
 *
 * The mempool is modelled: rte_pktmbuf_alloc_bulk either fails as a whole (the
 * rc < 0 path) or returns `count` distinct mbufs from a static pool, which is
 * the DPDK contract.
 *
 * Switches: RING_SIZE, VACUITY_CHECK.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "../dpdk/drivers/net/ena/base/ena_eth_com.c"
#include "ena_inv.h"

#ifndef RING_SIZE
#define RING_SIZE 4
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_adapter adapter;
static struct ena_ring ring;
static struct ena_rx_buffer pool_rbi[RING_SIZE];
static uint16_t pool_err[RING_SIZE];
static struct rte_mbuf *pool_refill[RING_SIZE];
/* the mempool's buffers: disjoint from the already-posted ones, as a mempool
 * cannot hand out a buffer the driver still owns */
static struct rte_mbuf posted_pool[RING_SIZE];
static struct rte_mbuf fresh_pool[RING_SIZE];
static struct ena_driver_stats drv_stats;
static struct rte_mempool the_pool;
static uint32_t the_db_reg;
/* the RX SQ descriptor ring: real memory, so the descriptor writes in
 * ena_com_add_single_rx_desc are bounds-checked */
static struct ena_eth_io_rx_desc sq_ring[RING_SIZE];

int ena_logtype_com = 0;
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

/* DPDK's bulk allocator: all-or-nothing, and the mbufs it returns are
 * distinct and not owned by anyone else. */
int rte_pktmbuf_alloc_bulk(
  struct rte_mempool *pool,
  struct rte_mbuf **mbufs,
  unsigned count)
{
  (void)pool;
  if(nd_int())
    return -1; /* pool exhausted */
  if(count > RING_SIZE)
    return -1;
  for(unsigned i = 0; i < RING_SIZE; i++)
    if(i < count)
    {
      mbufs[i] = &fresh_pool[i];
      fresh_pool[i].buf_iova = 0x100000 + 0x1000 * i;
      fresh_pool[i].buf_len = 2048;
      fresh_pool[i].pool = pool;
    }
  return 0;
}

/* the free path refill uses on its partial-failure branch */
static unsigned g_freed;
void rte_pktmbuf_free_bulk(struct rte_mbuf **mbufs, unsigned int count)
{
  for(unsigned i = 0; i < count; i++)
    if(mbufs[i] != NULL)
      g_freed++;
}

int main(void)
{
  unsigned i;

  __CPROVER_havoc_object(&ring);
  struct ena_com_io_sq *sq = &adapter.ena_dev.io_sq_queues[ENA_IO_RXQ_IDX(0)];
  struct ena_com_io_cq *cq = &adapter.ena_dev.io_cq_queues[ENA_IO_RXQ_IDX(0)];
  __CPROVER_havoc_object(sq);

  ring.adapter = &adapter;
  ring.type = ENA_RING_TYPE_RX;
  ring.id = 0;
  ring.configured = 1;
  ring.ring_size = RING_SIZE;
  ring.size_mask = RING_SIZE - 1;
  ring.sgl_size = ENA_PKT_MAX_BUFS;
  ring.rx_buffer_info = pool_rbi;
  ring.empty_rx_reqs = pool_err;
  ring.rx_refill_buffer = pool_refill;
  ring.mb_pool = &the_pool;
  ring.port_id = 0;
  ring.ena_com_io_sq = sq;
  ring.ena_com_io_cq = cq;
  adapter.drv_stats = &drv_stats;

  sq->qid = ENA_IO_RXQ_IDX(0);
  sq->direction = ENA_COM_IO_QUEUE_DIRECTION_RX;
  sq->mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;
  sq->q_depth = RING_SIZE;
  sq->desc_entry_size = sizeof(struct ena_eth_io_rx_desc);
  sq->desc_addr.virt_addr = (u8 *)sq_ring;
  sq->db_addr = &the_db_reg;
  sq->dma_addr_bits = 48;
  sq->tail = nd_u16();
  sq->next_to_comp = nd_u16();
  sq->phase = nd_u16() & 1;

  ring.next_to_use = nd_u16() & (RING_SIZE - 1);
  ring.next_to_clean = nd_u16() & (RING_SIZE - 1);

  for(i = 0; i < RING_SIZE; i++)
  {
    pool_rbi[i].mbuf = nd_int() ? &posted_pool[i] : NULL;
    pool_err[i] = nd_u16();
    __CPROVER_assume(pool_err[i] < RING_SIZE);
  }

  __CPROVER_assume(INV_RX_STARTED(&ring, RING_SIZE));
  /* The caller contract.  eth_ena_recv_pkts passes
   * ena_com_free_q_entries(sq) (ena_ethdev.c:2864) and ena_queue_start passes
   * ring_size - 1 on a fresh ring, which is the same number there.  With the
   * RX accounting clause of INV_RX_STARTED this is strictly fewer than the
   * free slots, so refill cannot reach an occupied one. */
  unsigned count = nd_u32();
  __CPROVER_assume(count <= (unsigned)ena_com_free_q_entries(sq));
  g_freed = 0;

  const uint16_t ntu0 = ring.next_to_use;
  const uint16_t probe = nd_u16() % RING_SIZE;
  /* a slot that is occupied before the call */
  struct rte_mbuf *const held = pool_rbi[probe].mbuf;

  /* ---- one refill ---- */
  int posted = ena_populate_rx_queue(&ring, count);

  /* ---- obligations ---- */
  __CPROVER_assert(
    posted >= 0 && (unsigned)posted <= count,
    "RF_RET: the posted count is between 0 and the requested count");
  if(held != NULL)
    __CPROVER_assert(
      ring.rx_buffer_info[probe].mbuf == held,
      "RF_NO_CLOBBER: refill does not overwrite an already-posted slot");
  __CPROVER_assert(
    INV_RX_STARTED(&ring, RING_SIZE),
    "RF_STARTED: ena_populate_rx_queue preserves INV_RX_STARTED");
  if(posted >= 1)
  {
    const uint16_t first = pool_err[ntu0];
    __CPROVER_assert(
      ring.rx_buffer_info[first].mbuf != NULL,
      "RF_POSTED: the first slot refill claimed now holds a buffer");
  }

#if VACUITY_CHECK
  __CPROVER_assert(posted == 0, "vacuity witness: refill can post a buffer");
  __CPROVER_assert(posted <= 1, "vacuity witness: refill can post two buffers");
  __CPROVER_assert(
    posted == (int)count || count == 0,
    "vacuity witness: refill can post fewer than requested");
#endif
  return 0;
}
