/*
 * Device lifecycle, the other half: ena_start over every queue.
 *
 * Together with harness_dev_lifecycle.c (ena_stop and ena_close) this closes
 * the lifecycle loop at the DEVICE level, not just per queue:
 *
 *   stop:  RUNNING, all queues started  ->  STOPPED, all queues INV_*STARTABLE
 *   start: STOPPED, all queues INV_*STARTABLE -> RUNNING, all queues INV_*STARTED
 *
 * so any sequence of device starts and stops is covered, for every queue, by
 * these two plus the per-queue data-path steps.
 *
 *   DS_STATE       on success the adapter is RUNNING
 *   DS_TX_STARTED  on success every TX queue satisfies INV_STARTED
 *   DS_RX_STARTED  on success every RX queue satisfies INV_RX_STARTED
 *   DS_QSTATE      on success every queue is marked STARTED to the ethdev layer
 *   DS_ERR_NO_HALF_START  on failure no queue is left started, so a later
 *                  stop or close does not run over a half-started device
 *
 * DS_ERR_NO_HALF_START is the error-path obligation; error paths are 61% of
 * the Unveil triggers.  ena_start's own unwind is the interesting part:
 * ena_queue_start_all cleans up its own partial work (err: while (i--)
 * ena_queue_stop), and ena_start then stops the other direction via
 * err_start_tx / err_rss_init.
 *
 * ABSTRACTION BOUNDARY, same as the per-queue start base cases:
 * ena_com_create_io_queue and ena_com_get_io_handlers are replaced by the
 * post-conditions harness_ena_com_create.c verifies against the real ena_com
 * code.  The refill inside RX start is the REAL ena_populate_rx_queue, proven
 * by harness_rx_refill.c.
 *
 * Rx queue interrupts are NOT requested here (intr_conf.rxq = 0), so
 * ena_setup_rx_intr returns 0 immediately; F18 is about that function and is
 * proven separately in harness_setup_rx_intr.c.
 *
 * Switches: RING, NQ, VACUITY_CHECK.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "../dpdk/drivers/net/ena/base/ena_eth_com.c"
#include "ena_inv.h"

#ifndef RING
#define RING 4
#endif
#ifndef NQ
#define NQ 1
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_adapter adapter;
static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;
static struct rte_pci_device pci_dev;
static uint8_t handle_storage[64];
static void *rx_queues[NQ], *tx_queues[NQ];

static struct ena_tx_buffer pool_tbi[NQ][RING];
static uint16_t pool_etr[NQ][RING];
static uint8_t pool_push[NQ][96];
static struct ena_rx_buffer pool_rbi[NQ][RING];
static uint16_t pool_err[NQ][RING];
static struct rte_mbuf *pool_refill[NQ][RING];
static struct rte_mbuf fresh[NQ][RING];
static struct ena_driver_stats drv_stats;
static struct rte_mempool the_pool;
static uint8_t reg_bar[4096];
static struct ena_eth_io_rx_desc rx_sq_ring[NQ][RING];

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
enum rte_proc_type_t rte_eal_process_type(void) { return RTE_PROC_PRIMARY; }

/* EAL services ena_start touches but no obligation depends on */
int rte_intr_disable(struct rte_intr_handle *h) { (void)h; return 0; }
int rte_intr_enable(struct rte_intr_handle *h) { (void)h; return 0; }
int rte_intr_dp_is_en(struct rte_intr_handle *h) { (void)h; return 0; }
int rte_intr_vec_list_index_get(const struct rte_intr_handle *h, unsigned i)
{
  (void)h;
  (void)i;
  return 0;
}
uint64_t rte_get_timer_hz(void) { return 1000000; }
int rte_timer_reset(
  struct rte_timer *t,
  uint64_t ticks,
  enum rte_timer_type type,
  unsigned tim_lcore,
  rte_timer_cb_t fct,
  void *arg)
{
  (void)t;
  (void)ticks;
  (void)type;
  (void)tim_lcore;
  (void)fct;
  (void)arg;
  return 0;
}
void rte_mempool_cache_flush(struct rte_mempool_cache *c, struct rte_mempool *mp)
{
  (void)c;
  (void)mp;
}
void rte_pktmbuf_free(struct rte_mbuf *m) { (void)m; }
void rte_pktmbuf_free_bulk(struct rte_mbuf **m, unsigned int n)
{
  (void)m;
  (void)n;
}
void rte_mempool_put(struct rte_mempool *mp, void *o)
{
  (void)mp;
  (void)o;
}

/* DPDK's bulk allocator, per its contract: all-or-nothing, distinct buffers */
int rte_pktmbuf_alloc_bulk(
  struct rte_mempool *pool,
  struct rte_mbuf **mbufs,
  unsigned count)
{
  (void)pool;
  if(nd_int() || count > NQ * RING)
    return -1; /* the pool may be exhausted: the short-refill path */
  for(unsigned i = 0; i < NQ * RING; i++)
    if(i < count)
    {
      struct rte_mbuf *m = &fresh[i / RING][i % RING];
      mbufs[i] = m;
      m->buf_iova = 0x100000 + 0x1000 * i;
      m->buf_len = 2048;
      m->pool = pool;
    }
  return 0;
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
    return ENA_COM_INVAL; /* the device may refuse any queue */

  sq = &ena_dev->io_sq_queues[ctx->qid];
  cq = &ena_dev->io_cq_queues[ctx->qid];
  cq->unmask_reg = (uint32_t *)(reg_bar + (nd_u16() % 4092));
  cq->numa_node_cfg_reg =
    nd_int() ? NULL : (uint32_t *)(reg_bar + (nd_u16() % 4092));
  sq->q_depth = ctx->queue_size;
  cq->q_depth = ctx->queue_size;
  sq->db_addr = (uint32_t *)(reg_bar + (nd_u16() % 4092));
  sq->mem_queue_type = ctx->mem_queue_type;
  sq->direction = ctx->direction;
  cq->direction = ctx->direction;
  sq->tail = 0;
  sq->next_to_comp = 0;
  sq->phase = 1;
  sq->dma_addr_bits = 48;
  if(ctx->direction == ENA_COM_IO_QUEUE_DIRECTION_RX)
  {
    /* RX uses host placement: a plain descriptor ring in host memory */
    const unsigned q = ENA_IO_RXQ_IDX_REV(ctx->qid);
    sq->desc_entry_size = sizeof(struct ena_eth_io_rx_desc);
    sq->desc_addr.virt_addr = (u8 *)(q < NQ ? rx_sq_ring[q] : rx_sq_ring[0]);
  }
  else
  {
    sq->llq_info.descs_per_entry = nd_u16();
    sq->llq_info.descs_num_before_header = nd_u16();
    sq->llq_info.disable_meta_caching = false;
    __CPROVER_assume(INV_DEV_LLQ(sq));
  }
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
  unsigned q, i;

  dev.data = &data;
  data.dev_private = &adapter;
  dev.device = &pci_dev.device;
  pci_dev.intr_handle = (struct rte_intr_handle *)handle_storage;
  data.rx_queues = rx_queues;
  data.tx_queues = tx_queues;
  data.nb_rx_queues = NQ;
  data.nb_tx_queues = NQ;
  /* no Rx queue interrupts requested, so ena_setup_rx_intr returns 0 at once;
   * F18 is about that function and is proven separately */
  data.dev_conf.intr_conf.rxq = 0;
  /* no RSS, so ena_rss_configure is not reached */
  data.dev_conf.rxmode.mq_mode = 0;
  adapter.drv_stats = &drv_stats;
  adapter.edev_data = &data;
  adapter.ena_dev.reg_bar = reg_bar;
  adapter.ena_dev.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;

  /* ---- a STOPPED adapter whose queues are startable ---- */
  adapter.state = ENA_ADAPTER_STATE_STOPPED;

  for(q = 0; q < NQ; q++)
  {
    struct ena_ring *t = &adapter.tx_ring[q];
    struct ena_ring *r = &adapter.rx_ring[q];

    t->adapter = &adapter;
    t->type = ENA_RING_TYPE_TX;
    t->id = q;
    t->configured = 1;
    t->ring_size = RING;
    t->size_mask = RING - 1;
    t->sgl_size = ENA_PKT_MAX_BUFS;
    t->tx_buffer_info = pool_tbi[q];
    t->empty_tx_reqs = pool_etr[q];
    t->push_buf_intermediate_buf = pool_push[q];
    t->tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
    t->numa_socket_id = 0;
    t->next_to_use = nd_u16();
    t->next_to_clean = nd_u16();
    tx_queues[q] = t;

    r->adapter = &adapter;
    r->type = ENA_RING_TYPE_RX;
    r->id = q;
    r->configured = 1;
    r->ring_size = RING;
    r->size_mask = RING - 1;
    r->sgl_size = ENA_PKT_MAX_BUFS;
    r->rx_buffer_info = pool_rbi[q];
    r->empty_rx_reqs = pool_err[q];
    r->rx_refill_buffer = pool_refill[q];
    r->mb_pool = &the_pool;
    r->numa_socket_id = 0;
    r->next_to_use = nd_u16();
    r->next_to_clean = nd_u16();
    rx_queues[q] = r;

    for(i = 0; i < RING; i++)
    {
      struct ena_tx_buffer tb; /* nondet */
      pool_tbi[q][i] = tb;
      pool_etr[q][i] = nd_u16();
      struct ena_rx_buffer rb; /* nondet */
      pool_rbi[q][i] = rb;
      pool_err[q][i] = nd_u16();
    }
    /* exactly what the start base cases assume */
    __CPROVER_assume(INV_STARTABLE(t, RING));
    __CPROVER_assume(INV_RX_STARTABLE(r, RING));
  }

  /* ---- one device start ---- */
  int rc = ena_start(&dev);

  if(rc == 0)
  {
    __CPROVER_assert(
      adapter.state == ENA_ADAPTER_STATE_RUNNING,
      "DS_STATE: a successful ena_start leaves the adapter RUNNING");
    for(q = 0; q < NQ; q++)
    {
      __CPROVER_assert(
        INV_STARTED(&adapter.tx_ring[q]),
        "DS_TX_STARTED: every TX queue satisfies INV_STARTED after start");
      __CPROVER_assert(
        INV_RX_STARTED(&adapter.rx_ring[q], RING),
        "DS_RX_STARTED: every RX queue satisfies INV_RX_STARTED after start");
      __CPROVER_assert(
        data.tx_queue_state[q] == RTE_ETH_QUEUE_STATE_STARTED &&
          data.rx_queue_state[q] == RTE_ETH_QUEUE_STATE_STARTED,
        "DS_QSTATE: every queue is reported started to the ethdev layer");
    }
  }
  else
  {
    /* error path: nothing may be left looking like a running queue, or a
     * later stop or close would run over a half-started device */
    __CPROVER_assert(
      adapter.state != ENA_ADAPTER_STATE_RUNNING,
      "DS_ERR_NO_HALF_START: a failed ena_start does not leave the adapter "
      "RUNNING");
    for(q = 0; q < NQ; q++)
      __CPROVER_assert(
        INV_STARTABLE(&adapter.tx_ring[q], RING) &&
          INV_RX_STARTABLE(&adapter.rx_ring[q], RING),
        "DS_ERR_NO_HALF_START: a failed ena_start leaves every queue startable");
  }

#ifdef VACUITY_CHECK
  __CPROVER_assert(rc != 0, "vacuity witness: a device start can succeed");
  __CPROVER_assert(rc == 0, "vacuity witness: a device start can fail");
#endif
  return 0;
}
