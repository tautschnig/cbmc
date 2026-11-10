/*
 * RX start base case: ena_queue_start on an RX ring takes INV_RX_STARTABLE to
 * INV_RX_STARTED.  The RX counterpart of harness_start_basecase.c.
 *
 * This is the part that makes the RX lifecycle compose: the setup base case
 * and the teardown step both establish INV_RX_STARTABLE, and the recv and
 * refill steps both assume INV_RX_STARTED, so this is the only link missing
 * between them.
 *
 * ena_queue_start (RX) does: ena_create_io_queue -- which fills
 * empty_rx_reqs with the identity and calls ena_com_create_io_queue and
 * ena_com_get_io_handlers -- then zeroes the indices, then
 * ena_populate_rx_queue(ring_size - 1), and on a short refill destroys the
 * device queue and fails.
 *
 * ABSTRACTION BOUNDARY: ena_com_create_io_queue and ena_com_get_io_handlers
 * are replaced by the post-conditions harness_ena_com_create.c verifies
 * against the real ena_com code, exactly as in the TX start base case and for
 * the same reason (they send admin commands and allocate DMA memory).  The
 * refill inside is the REAL ena_populate_rx_queue with the real
 * ena_com_add_single_rx_desc, since harness_rx_refill.c proves it.
 *
 *   RXST_STARTED   on success, INV_RX_STARTED holds
 *   RXST_FULL      on success the ring is filled to ring_size - 1 buffers,
 *                  which is what the device needs to receive anything
 *   RXST_ERRPATH   on failure, no usable consumer state is left behind
 *
 * Switches: RING, VACUITY_CHECK.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "../dpdk/drivers/net/ena/base/ena_eth_com.c"
#include "ena_inv.h"

#ifndef RING
#define RING 4
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_rx_buffer pool_rbi[RING];
static uint16_t pool_err[RING];
static struct rte_mbuf *pool_refill[RING];
static struct rte_mbuf fresh_pool[RING];
static struct ena_driver_stats drv_stats;
static struct rte_mempool the_pool;
static uint8_t pool_reg_bar[4096];
static struct ena_eth_io_rx_desc sq_ring[RING];

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
void rte_mempool_cache_flush(struct rte_mempool_cache *c, struct rte_mempool *mp)
{
  (void)c;
  (void)mp;
}
uint16_t rte_pktmbuf_data_room_size(struct rte_mempool *mp)
{
  (void)mp;
  return 2048;
}

/* ena_create_io_queue asks the EAL whether per-queue interrupts are enabled
 * and, if so, for this queue's MSI-X vector.  Left bodyless these cut the
 * path through the RX branch (DFCC issue A), so both outcomes are modelled. */
int rte_intr_dp_is_en(struct rte_intr_handle *h)
{
  (void)h;
  return nd_int() ? 1 : 0;
}
int rte_intr_vec_list_index_get(struct rte_intr_handle *h, unsigned int i)
{
  (void)h;
  (void)i;
  return (int)nd_u16(); /* the EAL's vector number */
}

/* DPDK's bulk allocator: all-or-nothing, distinct buffers (its contract) */
int rte_pktmbuf_alloc_bulk(
  struct rte_mempool *pool,
  struct rte_mbuf **mbufs,
  unsigned count)
{
  (void)pool;
  if(nd_int() || count > RING)
    return -1; /* the pool may be exhausted: that is the short-refill path */
  for(unsigned i = 0; i < RING; i++)
    if(i < count)
    {
      mbufs[i] = &fresh_pool[i];
      fresh_pool[i].buf_iova = 0x100000 + 0x1000 * i;
      fresh_pool[i].buf_len = 2048;
      fresh_pool[i].pool = pool;
    }
  return 0;
}
void rte_pktmbuf_free_bulk(struct rte_mbuf **mbufs, unsigned int count)
{
  (void)mbufs;
  (void)count;
}

/* ---- the abstraction boundary (see the header comment) ---- */
void ena_com_destroy_io_queue(struct ena_com_dev *ena_dev, uint16_t qid)
{
  if(qid >= ENA_TOTAL_NUM_QUEUES)
    return;
  ena_dev->io_sq_queues[qid].q_depth = 0;
  ena_dev->io_cq_queues[qid].q_depth = 0;
}

int ena_com_create_io_queue(
  struct ena_com_dev *ena_dev,
  struct ena_com_create_io_ctx *ctx)
{
  struct ena_com_io_sq *sq;
  struct ena_com_io_cq *cq;

  if(ctx->qid >= ENA_TOTAL_NUM_QUEUES)
    return ENA_COM_INVAL;
  if(nd_int())
    return ENA_COM_INVAL; /* the device may refuse */

  sq = &ena_dev->io_sq_queues[ctx->qid];
  cq = &ena_dev->io_cq_queues[ctx->qid];
  cq->unmask_reg = (uint32_t *)(pool_reg_bar + (nd_u16() % 4092));
  cq->numa_node_cfg_reg =
    nd_int() ? NULL : (uint32_t *)(pool_reg_bar + (nd_u16() % 4092));
  sq->q_depth = ctx->queue_size;
  cq->q_depth = ctx->queue_size;
  sq->db_addr = (uint32_t *)(pool_reg_bar + (nd_u16() % 4092));
  sq->mem_queue_type = ctx->mem_queue_type;
  sq->direction = ctx->direction;
  cq->direction = ctx->direction;
  sq->tail = 0;
  sq->next_to_comp = 0;
  sq->phase = 1;
  /* RX uses host placement, so the SQ is a plain descriptor ring in host
   * memory; give it real storage so the descriptor writes are checked */
  sq->desc_entry_size = sizeof(struct ena_eth_io_rx_desc);
  sq->desc_addr.virt_addr = (u8 *)sq_ring;
  sq->dma_addr_bits = 48;
  return 0;
}

int ena_com_get_io_handlers(
  struct ena_com_dev *ena_dev,
  uint16_t qid,
  struct ena_com_io_sq **io_sq,
  struct ena_com_io_cq **io_cq)
{
  if(qid >= ENA_TOTAL_NUM_QUEUES)
    return ENA_COM_INVAL;
  if(nd_int())
    return ENA_COM_INVAL;
  *io_sq = &ena_dev->io_sq_queues[qid];
  *io_cq = &ena_dev->io_cq_queues[qid];
  return 0;
}

int main(void)
{
  static struct rte_eth_dev dev;
  static struct rte_eth_dev_data data;
  static struct ena_adapter adapter;
  static struct rte_pci_device pci_dev;
  struct ena_ring *r;
  int rc;
  unsigned i;

  dev.data = &data;
  data.dev_private = &adapter;
  dev.device = &pci_dev.device;
  static void *rx_queues[1];
  data.rx_queues = rx_queues;
  data.nb_rx_queues = 1;
  adapter.drv_stats = &drv_stats;

  r = &adapter.rx_ring[0];

  /* ---- the pre-state: an INV_RX_STARTABLE ring (after setup, or after a
   * stop/reset).  The indices and the free list are whatever the previous
   * life of the queue left; ena_create_io_queue rewrites both. ---- */
  r->adapter = &adapter;
  r->type = ENA_RING_TYPE_RX;
  r->id = 0;
  r->configured = 1;
  r->ring_size = RING;
  r->size_mask = RING - 1;
  r->sgl_size = nd_u16();
  r->rx_buffer_info = pool_rbi;
  r->empty_rx_reqs = pool_err;
  r->rx_refill_buffer = pool_refill;
  r->mb_pool = &the_pool;
  r->port_id = 0;
  r->numa_socket_id = 0;
  r->next_to_use = nd_u16();
  r->next_to_clean = nd_u16();
  for(i = 0; i < RING; i++)
  {
    struct ena_rx_buffer b; /* nondet */
    pool_rbi[i] = b;
    pool_err[i] = nd_u16();
  }
  __CPROVER_assume(INV_RX_STARTABLE(r, RING));

  adapter.ena_dev.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;
  adapter.ena_dev.reg_bar = pool_reg_bar;

  rc = ena_queue_start(&dev, r);

#if VACUITY_CHECK
  /* both must FAIL, or one outcome is unreachable and its obligation vacuous */
  __CPROVER_assert(rc != 0, "vacuity witness: an rx start can succeed");
  __CPROVER_assert(rc == 0, "vacuity witness: an rx start can fail");
#endif

  if(rc == 0)
  {
    __CPROVER_assert(
      INV_RX_STARTED(r, RING),
      "base case: ena_queue_start establishes INV_RX_STARTED on an rx ring");
    unsigned posted = 0;
    for(i = 0; i < RING; i++)
      if(pool_rbi[i].mbuf != NULL)
        posted++;
    __CPROVER_assert(
      posted == RING - 1,
      "RXST_FULL: a started rx ring holds ring_size - 1 buffers");
  }
  else
  {
    /* error path: nothing may be left looking like a running queue */
    __CPROVER_assert(
      r->ena_com_io_sq == NULL || r->ena_com_io_sq->q_depth == 0 ||
        r->next_to_use == 0,
      "error path: a failed rx start leaves no usable consumer state");
  }
  return 0;
}
