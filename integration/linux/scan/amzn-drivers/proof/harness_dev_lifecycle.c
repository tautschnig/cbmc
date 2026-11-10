/*
 * Device lifecycle: ena_stop and ena_close, and the link to the per-queue
 * inductions.
 *
 * The per-queue results say that a TX queue cycles
 * INV_STARTABLE -> INV_STARTED -> INV_STARTABLE through start/xmit/cleanup/
 * stop, and likewise for RX.  Those are statements about ONE queue driven
 * directly.  The device operations drive every queue at once, so for the
 * per-queue results to mean anything at the device level, the device
 * operations have to establish the same predicates.  That is what this
 * harness checks:
 *
 *   LC_STOP_STATE       after ena_stop the adapter is STOPPED
 *   LC_STOP_TX_STARTABLE   every configured TX queue is INV_STARTABLE again
 *   LC_STOP_RX_STARTABLE   every configured RX queue is INV_RX_STARTABLE again
 *   LC_STOP_FREE_ONCE   no buffer is released twice across all queues
 *   LC_CLOSE_STATE      after ena_close the adapter is CLOSED
 *   LC_CLOSE_IDEMPOTENT a second ena_close is a no-op and frees nothing twice
 *   LC_DRV_STATS_ONCE   adapter->drv_stats is freed at most once
 *
 * LC_STOP_*_STARTABLE is the composition point: with it, ena_stop followed by
 * ena_start is covered by the per-queue base cases, for every queue.
 *
 * Scope: NQ queues of each direction, ring RING.  ena_com_destroy_io_queue is
 * modelled (it sends a destroy-queue admin command and frees DMA rings, and
 * writes no ring slot) exactly as in the per-queue stop steps.
 *
 * Switches: RING, NQ, VACUITY_CHECK, DOUBLE_CLOSE.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
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
static struct rte_mbuf tx_mbuf[NQ][RING];

static struct ena_rx_buffer pool_rbi[NQ][RING];
static uint16_t pool_err[NQ][RING];
static struct rte_mbuf *pool_refill[NQ][RING];
static struct rte_mbuf rx_mbuf[NQ][RING];

static struct ena_driver_stats drv_stats;
static struct rte_mempool the_pool;
static uint32_t db_regs[2 * NQ];

/* ghosts */
static unsigned g_drv_stats_frees;
static struct rte_mbuf *g_watch;
static unsigned g_watch_frees;

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

void rte_free(void *p)
{
  if(p == (void *)&drv_stats)
    g_drv_stats_frees++;
}

/* the TX free path */
void rte_pktmbuf_free(struct rte_mbuf *m)
{
  if(m == NULL)
    return;
  __CPROVER_assert(
    m != g_watch || g_watch_frees == 0,
    "LC_STOP_FREE_ONCE: no buffer is released twice across all queues");
  if(m == g_watch)
    g_watch_frees++;
}
/* the RX free path (rte_mbuf_raw_free -> rte_mempool_put) */
void rte_mempool_put(struct rte_mempool *mp, void *obj)
{
  (void)mp;
  if(obj == NULL)
    return;
  __CPROVER_assert(
    obj != (void *)g_watch || g_watch_frees == 0,
    "LC_STOP_FREE_ONCE: no buffer is released twice across all queues");
  if(obj == (void *)g_watch)
    g_watch_frees++;
}

/* The device-queue teardown: sends a destroy-queue admin command and frees
 * the DMA rings.  It writes no ring slot, so the ownership obligations do not
 * depend on its body; modelled as in the per-queue stop steps. */
void ena_com_destroy_io_queue(struct ena_com_dev *ena_dev, uint16_t qid)
{
  if(qid >= ENA_TOTAL_NUM_QUEUES)
    return;
  ena_dev->io_sq_queues[qid].q_depth = 0;
  ena_dev->io_cq_queues[qid].q_depth = 0;
}
int ena_com_dev_reset(
  struct ena_com_dev *ena_dev,
  enum ena_regs_reset_reason_types reason)
{
  (void)ena_dev;
  (void)reason;
  return nd_int() ? ENA_COM_INVAL : 0; /* the device may refuse */
}

/* This harness is about the PRIMARY process.  ena_stop and ena_close both
 * return early in a secondary one (ena_ethdev.c:1373, :901) without touching
 * the adapter state, which is correct behaviour and not what is under test. */
enum rte_proc_type_t rte_eal_process_type(void) { return RTE_PROC_PRIMARY; }

/* ena_close's admin-layer teardown.  Each releases admin-queue or host-info
 * resources and writes no ring slot, so the ownership and startability
 * obligations do not depend on their bodies.  Defined rather than
 * body-removed, so no call is left bodyless and no path is cut. */
void ena_com_set_admin_running_state(struct ena_com_dev *d, bool state)
{
  (void)d;
  (void)state;
}
void ena_com_rss_destroy(struct ena_com_dev *d) { (void)d; }
void ena_com_delete_debug_area(struct ena_com_dev *d) { (void)d; }
void ena_com_delete_host_info(struct ena_com_dev *d) { (void)d; }
void ena_com_abort_admin_commands(struct ena_com_dev *d) { (void)d; }
void ena_com_wait_for_abort_completion(struct ena_com_dev *d) { (void)d; }
void ena_com_admin_destroy(struct ena_com_dev *d) { (void)d; }
void ena_com_mmio_reg_read_request_destroy(struct ena_com_dev *d) { (void)d; }
void ena_com_delete_customer_metrics_buffer(struct ena_com_dev *d) { (void)d; }

/* EAL interrupt and timer services: no obligation depends on them */
int rte_intr_disable(struct rte_intr_handle *h) { (void)h; return 0; }
int rte_intr_enable(struct rte_intr_handle *h) { (void)h; return 0; }
void rte_intr_efd_disable(struct rte_intr_handle *h) { (void)h; }
void rte_intr_vec_list_free(struct rte_intr_handle *h) { (void)h; }
void rte_timer_stop_sync(struct rte_timer *t) { (void)t; }
int rte_intr_callback_unregister_sync(
  struct rte_intr_handle *h,
  rte_intr_callback_fn cb,
  void *arg)
{
  (void)h;
  (void)cb;
  (void)arg;
  return 0;
}
int rte_eal_alarm_cancel(rte_eal_alarm_callback cb, void *arg)
{
  (void)cb;
  (void)arg;
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
  /* rx_queue_state/tx_queue_state are inline arrays in rte_eth_dev_data,
   * not pointers, so ena_stop writes them in place. */
  adapter.drv_stats = &drv_stats;
  adapter.edev_data = &data;

  /* the watched buffer is one arbitrary slot of one arbitrary queue */
  const uint16_t wq = nd_u16() % NQ;
  const uint16_t ws = nd_u16() % RING;
  const bool watch_tx = nd_int() ? true : false;
  g_watch = watch_tx ? &tx_mbuf[wq][ws] : &rx_mbuf[wq][ws];
  g_watch_frees = 0;
  g_drv_stats_frees = 0;

  /* ---- a RUNNING adapter whose queues satisfy the per-queue invariants ---- */
  adapter.state = ENA_ADAPTER_STATE_RUNNING;
  adapter.trigger_reset = nd_int() ? true : false;
  adapter.control_path_poll_interval = nd_int() ? 0 : 1000;

  for(q = 0; q < NQ; q++)
  {
    struct ena_ring *t = &adapter.tx_ring[q];
    struct ena_ring *r = &adapter.rx_ring[q];
    struct ena_com_io_sq *tsq = &adapter.ena_dev.io_sq_queues[ENA_IO_TXQ_IDX(q)];
    struct ena_com_io_cq *tcq = &adapter.ena_dev.io_cq_queues[ENA_IO_TXQ_IDX(q)];
    struct ena_com_io_sq *rsq = &adapter.ena_dev.io_sq_queues[ENA_IO_RXQ_IDX(q)];
    struct ena_com_io_cq *rcq = &adapter.ena_dev.io_cq_queues[ENA_IO_RXQ_IDX(q)];

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
    t->ena_com_io_sq = tsq;
    t->ena_com_io_cq = tcq;
    t->tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
    tsq->mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
    tsq->q_depth = RING;
    tsq->db_addr = &db_regs[2 * q];
    /* the LLQ geometry device init established, and the SQ cursors.  Without
     * these INV_COM's INV_DEV_LLQ clause (descs_per_entry >= 1) is false on a
     * zero-initialised SQ and the whole entry state is unsatisfiable, which a
     * reachability probe caught. */
    tsq->llq_info.descs_per_entry = nd_u16();
    tsq->llq_info.descs_num_before_header = nd_u16();
    tsq->llq_info.disable_meta_caching = false;
    tsq->tail = nd_u16();
    tsq->next_to_comp = nd_u16();
    __CPROVER_assume(INV_DEV_LLQ(tsq));
    t->next_to_use = nd_u16() & (RING - 1);
    t->next_to_clean = nd_u16() & (RING - 1);
    tx_queues[q] = t;
    data.tx_queue_state[q] = RTE_ETH_QUEUE_STATE_STARTED;

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
    r->ena_com_io_sq = rsq;
    r->ena_com_io_cq = rcq;
    rsq->q_depth = RING;
    rsq->db_addr = &db_regs[2 * q + 1];
    rsq->tail = nd_u16();
    rsq->next_to_comp = nd_u16();
    r->next_to_use = nd_u16() & (RING - 1);
    r->next_to_clean = nd_u16() & (RING - 1);
    rx_queues[q] = r;
    data.rx_queue_state[q] = RTE_ETH_QUEUE_STATE_STARTED;

    for(i = 0; i < RING; i++)
    {
      pool_tbi[q][i].mbuf = nd_int() ? &tx_mbuf[q][i] : NULL;
      pool_etr[q][i] = nd_u16();
      __CPROVER_assume(pool_etr[q][i] < RING);
      pool_rbi[q][i].mbuf = nd_int() ? &rx_mbuf[q][i] : NULL;
      rx_mbuf[q][i].pool = &the_pool;
      pool_err[q][i] = nd_u16();
      __CPROVER_assume(pool_err[q][i] < RING);
    }
    /* exactly the per-queue entry predicates */
    __CPROVER_assume(INV_STARTED(t));
    __CPROVER_assume(INV_FREE_LIST(t, RING));
    __CPROVER_assume(INV_RX_STARTED(r, RING));
  }

  /* ---- one device stop ---- */
  int rc_stop = ena_stop(&dev);
  (void)rc_stop;

  __CPROVER_assert(
    adapter.state == ENA_ADAPTER_STATE_STOPPED,
    "LC_STOP_STATE: ena_stop leaves the adapter STOPPED");
  for(q = 0; q < NQ; q++)
  {
    __CPROVER_assert(
      INV_STARTABLE(&adapter.tx_ring[q], RING),
      "LC_STOP_TX_STARTABLE: every TX queue is startable again after stop");
    __CPROVER_assert(
      INV_RX_STARTABLE(&adapter.rx_ring[q], RING),
      "LC_STOP_RX_STARTABLE: every RX queue is startable again after stop");
  }

  /* ---- then close ---- */
  int rc_close = ena_close(&dev);
  (void)rc_close;
  __CPROVER_assert(
    adapter.state == ENA_ADAPTER_STATE_CLOSED,
    "LC_CLOSE_STATE: ena_close leaves the adapter CLOSED");
  __CPROVER_assert(
    g_drv_stats_frees <= 1,
    "LC_DRV_STATS_ONCE: adapter->drv_stats is freed at most once");

#ifdef DOUBLE_CLOSE
  /* rte_eth_dev_close may be reached twice (directly, and through the PMD
   * remove path); the second must be a no-op */
  const unsigned frees_before = g_drv_stats_frees;
  int rc2 = ena_close(&dev);
  __CPROVER_assert(
    rc2 == 0 && g_drv_stats_frees == frees_before,
    "LC_CLOSE_IDEMPOTENT: a second ena_close frees nothing and returns 0");
#endif

#ifdef VACUITY_CHECK
  __CPROVER_assert(
    g_watch_frees == 0, "vacuity witness: a stop can free the watched buffer");
  __CPROVER_assert(
    g_drv_stats_frees == 0, "vacuity witness: close can free drv_stats");
  __CPROVER_assert(
    !adapter.trigger_reset,
    "vacuity witness: a stop can run with a reset pending");
#endif
  return 0;
}
