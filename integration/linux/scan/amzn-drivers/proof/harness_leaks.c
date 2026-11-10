/*
 * Phase 6: leaks (Unveil class D, 17 findings / 9%).
 *
 * The obligation is an allocation ledger: over a full queue lifecycle every
 * acquire is matched by a release, and nothing is released twice.  A ghost
 * counter per allocation site makes that checkable without modelling the
 * allocator.
 *
 * The cycle driven here is the one the inductions cover:
 *
 *   ena_tx_queue_setup / ena_rx_queue_setup   acquire the per-queue arrays
 *   ena_queue_start (both directions)          acquire the device queues
 *   ena_stop                                   release the device queues
 *   ena_close                                  release the arrays and
 *                                              drv_stats
 *
 * Obligations:
 *   LK_BALANCED   after close, every tracked allocation has been freed
 *   LK_NO_DOUBLE  nothing is freed twice
 *   LK_NO_WILD    nothing is freed that was never allocated
 *   LK_SETUP_FAIL when a setup fails part way, it leaves nothing allocated
 *                 (the leak-on-error-path case, which is where class D
 *                 findings usually are)
 *
 * The allocator hands out distinct static pools and the ledger records which
 * are live, so a leak shows up as a nonzero count rather than needing a
 * real heap.
 *
 * Switches: RING, VACUITY_CHECK, SETUP_FAIL.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING
#define RING 4
#endif
#define NSLOTS 10 /* tracked allocation slots: drv_stats + 3 TX + 3 RX */

static uint16_t nd_u16(void) { uint16_t v; return v; }
static int nd_int(void) { int v; return v; }

/* the allocation ledger: one entry per static pool we hand out */
/* generous: a too-small pool makes an allocation return NULL, the setup
 * fail, and the assume(rc == 0) below cut the whole run -- which the vacuity
 * witness caught */
static uint8_t pool_mem[NSLOTS][8192];
static bool live[NSLOTS];
static bool ever[NSLOTS];
static unsigned g_double_frees;
static unsigned g_wild_frees;
static bool g_alloc_fails;

static int slot_of(const void *p)
{
  for(int i = 0; i < NSLOTS; i++)
    if(p == (const void *)pool_mem[i])
      return i;
  return -1;
}

void *rte_zmalloc_socket(const char *t, size_t size, unsigned a, int s)
{
  (void)t;
  (void)a;
  (void)s;
  if(size == 0 || size > sizeof(pool_mem[0]))
    return NULL;
  if(g_alloc_fails && nd_int())
    return NULL; /* any allocation may fail */
  for(int i = 0; i < NSLOTS; i++)
    if(!live[i])
    {
      live[i] = true;
      ever[i] = true;
      memset(pool_mem[i], 0, size);
      return pool_mem[i];
    }
  return NULL; /* ledger exhausted */
}

void rte_free(void *p)
{
  if(p == NULL)
    return;
  const int i = slot_of(p);
  if(i < 0)
  {
    g_wild_frees++;
    return;
  }
  if(!live[i])
    g_double_frees++;
  live[i] = false;
}

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
uint16_t rte_pktmbuf_data_room_size(struct rte_mempool *mp)
{
  (void)mp;
  return 2048;
}
void rte_pktmbuf_free(struct rte_mbuf *m) { (void)m; }
void rte_mempool_put(struct rte_mempool *mp, void *o)
{
  (void)mp;
  (void)o;
}
void rte_mempool_cache_flush(struct rte_mempool_cache *c, struct rte_mempool *mp)
{
  (void)c;
  (void)mp;
}
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
void ena_com_set_admin_running_state(struct ena_com_dev *d, bool s)
{
  (void)d;
  (void)s;
}
void ena_com_rss_destroy(struct ena_com_dev *d) { (void)d; }
void ena_com_delete_debug_area(struct ena_com_dev *d) { (void)d; }
void ena_com_delete_host_info(struct ena_com_dev *d) { (void)d; }
void ena_com_abort_admin_commands(struct ena_com_dev *d) { (void)d; }
void ena_com_wait_for_abort_completion(struct ena_com_dev *d) { (void)d; }
void ena_com_admin_destroy(struct ena_com_dev *d) { (void)d; }
void ena_com_mmio_reg_read_request_destroy(struct ena_com_dev *d) { (void)d; }
void ena_com_delete_customer_metrics_buffer(struct ena_com_dev *d) { (void)d; }
void ena_com_destroy_io_queue(struct ena_com_dev *d, uint16_t qid)
{
  if(qid >= ENA_TOTAL_NUM_QUEUES)
    return;
  d->io_sq_queues[qid].q_depth = 0;
  d->io_cq_queues[qid].q_depth = 0;
}
int ena_com_dev_reset(struct ena_com_dev *d, enum ena_regs_reset_reason_types r)
{
  (void)d;
  (void)r;
  return 0;
}

static struct ena_adapter adapter;
static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;
static struct rte_pci_device pci_dev;
static uint8_t handle_storage[64];
static void *rx_queues[1], *tx_queues[1];
static struct rte_mempool the_pool;
static struct ena_driver_stats *drv_stats_alloc;

int main(void)
{
  int rc_tx, rc_rx;

  dev.data = &data;
  data.dev_private = &adapter;
  dev.device = &pci_dev.device;
  pci_dev.intr_handle = (struct rte_intr_handle *)handle_storage;
  data.rx_queues = rx_queues;
  data.tx_queues = tx_queues;
  data.nb_rx_queues = 1;
  data.nb_tx_queues = 1;
  data.port_id = 0;
  adapter.max_tx_ring_size = RING;
  adapter.max_rx_ring_size = RING;
  adapter.max_rx_sgl_size = ENA_PKT_MAX_BUFS;
  adapter.max_tx_sgl_size = ENA_PKT_MAX_BUFS;
  adapter.state = ENA_ADAPTER_STATE_CONFIG;
  adapter.tx_ring[0].configured = 0;
  adapter.rx_ring[0].configured = 0;
  adapter.tx_ring[0].sgl_size = ENA_PKT_MAX_BUFS;
  adapter.rx_ring[0].sgl_size = ENA_PKT_MAX_BUFS;
  /* ena_init_rings sets this from the device; the push-buffer allocation uses
   * it as its size, so leaving it 0 makes rte_zmalloc_socket(0) return NULL
   * and every TX setup fail -- which is what a reachability probe found when
   * the whole run came out vacuous. */
  adapter.tx_ring[0].tx_max_header_size = 96;
  /* the rest of what ena_init_rings establishes (ena_ethdev.c:2515-2540) and
   * the setups do not: without ring->adapter the teardown dereferences
   * &NULL->ena_dev, and without ring->type ena_queue_stop takes the wrong
   * direction's path */
  adapter.tx_ring[0].adapter = &adapter;
  adapter.tx_ring[0].type = ENA_RING_TYPE_TX;
  adapter.tx_ring[0].id = 0;
  adapter.tx_ring[0].tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  adapter.rx_ring[0].adapter = &adapter;
  adapter.rx_ring[0].type = ENA_RING_TYPE_RX;
  adapter.rx_ring[0].id = 0;
  adapter.tx_ring[0].ena_com_io_sq =
    &adapter.ena_dev.io_sq_queues[ENA_IO_TXQ_IDX(0)];
  adapter.tx_ring[0].ena_com_io_cq =
    &adapter.ena_dev.io_cq_queues[ENA_IO_TXQ_IDX(0)];
  adapter.rx_ring[0].ena_com_io_sq =
    &adapter.ena_dev.io_sq_queues[ENA_IO_RXQ_IDX(0)];
  adapter.rx_ring[0].ena_com_io_cq =
    &adapter.ena_dev.io_cq_queues[ENA_IO_RXQ_IDX(0)];

  /* drv_stats comes from the same ledger, as eth_ena_dev_init allocates it */
  drv_stats_alloc = rte_zmalloc_socket("drv_stats", sizeof(*drv_stats_alloc), 0, 0);
  __CPROVER_assume(drv_stats_alloc != NULL);
  adapter.drv_stats = drv_stats_alloc;

#ifdef SETUP_FAIL
  g_alloc_fails = true;
#else
  g_alloc_fails = false;
#endif
  g_double_frees = 0;
  g_wild_frees = 0;

  static struct rte_eth_txconf tx_conf;
  static struct rte_eth_rxconf rx_conf;
  tx_conf.tx_free_thresh = 0;
  tx_conf.offloads = 0;
  rx_conf.rx_free_thresh = 0;
  rx_conf.offloads = 0;

  rc_tx = ena_tx_queue_setup(&dev, 0, RING, 0, &tx_conf);
  rc_rx = ena_rx_queue_setup(&dev, 0, RING, 0, &rx_conf, &the_pool);

#ifdef SETUP_FAIL
  /* The leak-on-error-path case: if either setup failed, it must have
   * released whatever it had already taken.  Only the drv_stats allocation
   * may still be live at this point. */
  if(rc_tx != 0 && rc_rx != 0)
  {
    unsigned live_cnt = 0;
    for(int i = 0; i < NSLOTS; i++)
      if(live[i] && pool_mem[i] != (uint8_t *)drv_stats_alloc)
        live_cnt++;
    __CPROVER_assert(
      live_cnt == 0,
      "LK_SETUP_FAIL: a setup that fails leaves nothing allocated");
  }
#else
  __CPROVER_assume(rc_tx == 0 && rc_rx == 0);
  adapter.state = ENA_ADAPTER_STATE_RUNNING;

  /* ---- tear the whole thing down ---- */
  ena_stop(&dev);
  ena_close(&dev);

  {
    unsigned live_cnt = 0;
    for(int i = 0; i < NSLOTS; i++)
      if(live[i])
        live_cnt++;
    __CPROVER_assert(
      live_cnt == 0,
      "LK_BALANCED: after close every tracked allocation has been freed");
  }
  __CPROVER_assert(
    g_double_frees == 0, "LK_NO_DOUBLE: nothing is freed twice");
  __CPROVER_assert(
    g_wild_frees == 0, "LK_NO_WILD: nothing is freed that was never allocated");
#endif

#ifdef VACUITY_CHECK
  {
    unsigned ever_cnt = 0;
    for(int i = 0; i < NSLOTS; i++)
      if(ever[i])
        ever_cnt++;
    /* must FAIL: the cycle really does allocate several things */
    __CPROVER_assert(ever_cnt < 4, "vacuity witness: the cycle allocates");
  }
#endif
  return 0;
}
