/*
 * F21: a stop/start cycle leaves trigger_reset set, permanently disabling the
 * watchdog on the restarted port (DPDK ENA).
 *
 * adapter->trigger_reset is set by ena_trigger_reset (ena_ethdev.c:648) from
 * the watchdog, the data path and the control path.  Nothing in the DPDK
 * driver ever clears it: the only place it becomes false again is the
 * memset(adapter, 0, ...) in eth_ena_dev_init (:2296), reached only through
 * probe or rte_eth_dev_reset.
 *
 * ena_stop acts on the flag and leaves it set (:1382-1386):
 *
 *     if (adapter->trigger_reset) {
 *             rc = ena_com_dev_reset(ena_dev, adapter->reset_reason);
 *             ...
 *     }
 *
 * The device has now been reset, so the flag is stale.  But
 * rte_eth_dev_stop followed by rte_eth_dev_start is a legal sequence that
 * does NOT go through eth_ena_dev_init, so on the restarted port the flag is
 * still true, and ena_timer_wd_callback begins with (:2083)
 *
 *     if (unlikely(adapter->trigger_reset))
 *             return;
 *
 * so every subsequent tick returns immediately.  The keep-alive check, the
 * admin-state check and the missing-Tx-completion check never run again.
 * That last one is the mechanism F15 relies on for recovery, so after this
 * the driver can no longer detect a stuck queue.
 *
 * Both sibling drivers clear the flag explicitly: Linux
 * clear_bit(ENA_FLAG_TRIGGER_RESET, ...) (ena_netdev.c:3292) and FreeBSD
 * ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_TRIGGER_RESET, ...) (ena.c:3617).
 *
 * The obligation is that a watchdog tick on a running port still performs its
 * checks.  WD_RUNS is observed through a ghost set by the first check the
 * tick reaches.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING
#define RING 4
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static uint64_t nd_u64(void) { uint64_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_adapter adapter;
static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;
static struct rte_pci_device pci_dev;
static uint8_t handle_storage[64];
static void *tx_queues[1], *rx_queues[1];
static struct ena_tx_buffer pool_tbi[RING];
static uint16_t pool_etr[RING];
static uint8_t pool_push[96];
static struct ena_rx_buffer pool_rbi[RING];
static uint16_t pool_err[RING];
static struct rte_mbuf *pool_refill[RING];
static struct ena_driver_stats drv_stats;
static struct rte_mempool the_pool;
static uint32_t db_regs[2];

/* ghost: did the watchdog actually perform a check this tick? */
static bool g_wd_ran;

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
void rte_pktmbuf_free(struct rte_mbuf *m) { (void)m; }
void rte_mempool_put(struct rte_mempool *mp, void *o)
{
  (void)mp;
  (void)o;
}
int rte_intr_disable(struct rte_intr_handle *h) { (void)h; return 0; }
int rte_intr_enable(struct rte_intr_handle *h) { (void)h; return 0; }
void rte_intr_efd_disable(struct rte_intr_handle *h) { (void)h; }
void rte_intr_vec_list_free(struct rte_intr_handle *h) { (void)h; }
void rte_timer_stop_sync(struct rte_timer *t) { (void)t; }
int rte_eth_dev_callback_process(
  struct rte_eth_dev *d,
  enum rte_eth_event_type e,
  void *p)
{
  (void)d;
  (void)e;
  (void)p;
  return 0;
}
int ena_com_dev_reset(struct ena_com_dev *d, enum ena_regs_reset_reason_types r)
{
  (void)d;
  (void)r;
  return 0; /* the device accepts the reset ena_stop issues */
}
void ena_com_destroy_io_queue(struct ena_com_dev *d, uint16_t qid)
{
  if(qid >= ENA_TOTAL_NUM_QUEUES)
    return;
  d->io_sq_queues[qid].q_depth = 0;
  d->io_cq_queues[qid].q_depth = 0;
}

/* The first check a watchdog tick reaches.  Observing it tells us whether the
 * tick did any work, which is exactly what F21 is about.  The real one is
 * replaced by the runner. */
bool model_admin_running_state(struct ena_com_dev *d)
{
  (void)d;
  g_wd_ran = true;
  return true;
}

int main(void)
{
  unsigned i;

  dev.data = &data;
  data.dev_private = &adapter;
  dev.device = &pci_dev.device;
  pci_dev.intr_handle = (struct rte_intr_handle *)handle_storage;
  data.tx_queues = tx_queues;
  data.rx_queues = rx_queues;
  data.nb_tx_queues = 1;
  data.nb_rx_queues = 1;
  adapter.drv_stats = &drv_stats;
  adapter.edev_data = &data;

  struct ena_ring *t = &adapter.tx_ring[0];
  struct ena_com_io_sq *sq = &adapter.ena_dev.io_sq_queues[ENA_IO_TXQ_IDX(0)];
  struct ena_com_io_cq *cq = &adapter.ena_dev.io_cq_queues[ENA_IO_TXQ_IDX(0)];
  t->adapter = &adapter;
  t->type = ENA_RING_TYPE_TX;
  t->id = 0;
  t->configured = 1;
  t->ring_size = RING;
  t->size_mask = RING - 1;
  t->sgl_size = ENA_PKT_MAX_BUFS;
  t->tx_buffer_info = pool_tbi;
  t->empty_tx_reqs = pool_etr;
  t->push_buf_intermediate_buf = pool_push;
  t->ena_com_io_sq = sq;
  t->ena_com_io_cq = cq;
  t->tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  sq->mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  sq->q_depth = RING;
  sq->db_addr = &db_regs[0];
  sq->llq_info.descs_per_entry = 8;
  sq->llq_info.descs_num_before_header = 2;
  sq->llq_info.disable_meta_caching = false;
  t->next_to_use = 0;
  t->next_to_clean = 0;
  tx_queues[0] = t;

  struct ena_ring *r = &adapter.rx_ring[0];
  r->adapter = &adapter;
  r->type = ENA_RING_TYPE_RX;
  r->id = 0;
  r->configured = 1;
  r->ring_size = RING;
  r->size_mask = RING - 1;
  r->sgl_size = ENA_PKT_MAX_BUFS;
  r->rx_buffer_info = pool_rbi;
  r->empty_rx_reqs = pool_err;
  r->rx_refill_buffer = pool_refill;
  r->mb_pool = &the_pool;
  r->ena_com_io_sq = &adapter.ena_dev.io_sq_queues[ENA_IO_RXQ_IDX(0)];
  r->ena_com_io_cq = &adapter.ena_dev.io_cq_queues[ENA_IO_RXQ_IDX(0)];
  adapter.ena_dev.io_sq_queues[ENA_IO_RXQ_IDX(0)].q_depth = RING;
  rx_queues[0] = r;
  for(i = 0; i < RING; i++)
  {
    pool_etr[i] = i;
    pool_err[i] = i;
  }

  /* the watchdog's own configuration, as ena_dev_configure leaves it */
  adapter.missing_tx_completion_to = nd_u64();
  adapter.missing_tx_completion_budget = 1;
  adapter.tx_cleanup_stall_delay = nd_u64();
  adapter.last_tx_comp_qid = 0;
  adapter.keep_alive_timeout = nd_u64();
  adapter.timestamp_wd = nd_u64();
  adapter.active_aenq_groups = nd_u32();

  /* A reset has been requested -- by the watchdog, the data path or the
   * control path, it does not matter which. */
  adapter.state = ENA_ADAPTER_STATE_RUNNING;
#ifdef NO_PRIOR_RESET
  /* control case: no reset was ever requested, so the watchdog should work
   * normally after the stop/start cycle.  If this also failed, the harness
   * would be measuring something other than the stale flag. */
  adapter.trigger_reset = false;
#else
  ena_trigger_reset(&adapter, ENA_REGS_RESET_KEEP_ALIVE_TO);
  __CPROVER_assume(adapter.trigger_reset);
#endif

  /* ---- the application does what the API allows: stop, then start ---- */
  ena_stop(&dev);
  /* ena_start is not called here: the point is the flag's value after stop,
   * which a restart does not change, and calling it would drag the whole
   * queue-start machinery into this harness.  The adapter is put back into
   * the state a successful restart leaves. */
  adapter.state = ENA_ADAPTER_STATE_RUNNING;

#ifdef CLEAR_ON_STOP
  /* the proposed fix: clear the flag once ena_stop has acted on it, which is
   * what Linux (ena_netdev.c:3292) and FreeBSD (ena.c:3617) do */
  adapter.trigger_reset = false;
#endif

  /* ---- one watchdog tick on the restarted port ---- */
  g_wd_ran = false;
  ena_timer_wd_callback(NULL, &dev);

  /* ---- the obligation ---- */
  __CPROVER_assert(
    g_wd_ran,
    "F21_WD_RUNS: a watchdog tick on a running port still performs its "
    "checks after a stop/start cycle");

#ifdef VACUITY_CHECK
  /* must FAIL: the tick is reached at all */
  __CPROVER_assert(
    adapter.state != ENA_ADAPTER_STATE_RUNNING,
    "vacuity witness: the restarted port is running");
#endif
  return 0;
}
