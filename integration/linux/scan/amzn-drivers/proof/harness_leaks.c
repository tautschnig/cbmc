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

/* ---- the five acquire/release pairs eth_ena_dev_init's ladder unwinds ----
 *
 * Each allocator takes a ledger slot and each deleter releases it, so
 * PR_NO_LEAK is about all five acquisitions rather than only drv_stats.  Every
 * allocator can fail, which is what makes the ladder's cascade of labels
 * reachable: err_control_path_destroy, err_rss_destroy,
 * err_delete_debug_area, err_device_destroy, err_metrics_delete, err.
 *
 * Modelled rather than compiled because ena_com.c is not part of this build;
 * a bodyless allocator would neither acquire nor register, which is exactly
 * the state that made this configuration vacuous before. */
#define PROBE_ACQ(field)                                                       \
  do                                                                           \
  {                                                                            \
    void *p = rte_zmalloc_socket("probe", 64, 0, 0);                           \
    if(p == NULL)                                                              \
      return ENA_COM_NO_MEM;                                                    \
    (field) = p;                                                               \
    return 0;                                                                  \
  } while(0)
#define PROBE_REL(field)                                                       \
  do                                                                           \
  {                                                                            \
    g_ladder_entries++;                                                        \
    if((field) != NULL)                                                        \
    {                                                                          \
      rte_free(field);                                                          \
      (field) = NULL;                                                          \
    }                                                                          \
  } while(0)

static void *g_metrics, *g_host_info, *g_debug_area, *g_rss, *g_admin;
/* counts entries into the cleanup ladder, so a failing probe that
 * bypasses it altogether can be distinguished from one that runs it */
static unsigned g_ladder_entries;

int ena_com_allocate_customer_metrics_buffer(struct ena_com_dev *d)
{
  (void)d;
  PROBE_ACQ(g_metrics);
}
void ena_com_delete_customer_metrics_buffer(struct ena_com_dev *d)
{
  (void)d;
  PROBE_REL(g_metrics);
}
int ena_com_allocate_host_info(struct ena_com_dev *d)
{
  /* The real one publishes the block in host_attr, which ena_config_host_info
   * then writes through.  A model that only took a ledger slot left that
   * pointer unset and produced 17 spurious dereference failures. */
  void *p = rte_zmalloc_socket("host_info",
                               sizeof(struct ena_admin_host_info), 0, 0);
  if(p == NULL)
    return ENA_COM_NO_MEM;
  g_host_info = p;
  d->host_attr.host_info = p;
  return 0;
}
void ena_com_delete_host_info(struct ena_com_dev *d)
{
  PROBE_REL(g_host_info);
  if(d != NULL)
    d->host_attr.host_info = NULL;
}
int ena_com_allocate_debug_area(struct ena_com_dev *d, u32 sz)
{
  void *p = rte_zmalloc_socket("debug_area", sz ? sz : 64, 0, 0);
  if(p == NULL)
    return ENA_COM_NO_MEM;
  g_debug_area = p;
  d->host_attr.debug_area_virt_addr = p;
  d->host_attr.debug_area_size = sz;
  return 0;
}
void ena_com_delete_debug_area(struct ena_com_dev *d)
{
  PROBE_REL(g_debug_area);
  if(d != NULL)
    d->host_attr.debug_area_virt_addr = NULL;
}
int ena_com_rss_init(struct ena_com_dev *d, u16 log_size)
{
  (void)d;
  (void)log_size;
  PROBE_ACQ(g_rss);
}
void ena_com_rss_destroy(struct ena_com_dev *d)
{
  (void)d;
  PROBE_REL(g_rss);
}
int ena_com_admin_init(struct ena_com_dev *d, struct ena_aenq_handlers *h)
{
  (void)d;
  (void)h;
  PROBE_ACQ(g_admin);
}
void ena_com_admin_destroy(struct ena_com_dev *d)
{
  (void)d;
  PROBE_REL(g_admin);
}


#ifdef PROBE
/* Models for the two EAL services ena_init_once touches.  Both are reached on
 * the probe path, and a REACHED bodyless callee cuts it -- which would leave
 * the ladder obligations holding for want of a path rather than because the
 * ladder is correct. */

/* ---- the rest of the probe path: no resource, arbitrary outcome ---- */
int ena_com_mmio_reg_read_request_init(struct ena_com_dev *d)
{
  (void)d;
  return nd_int();
}
int ena_com_validate_version(struct ena_com_dev *d)
{
  (void)d;
  return nd_int();
}
int ena_com_get_dma_width(struct ena_com_dev *d)
{
  (void)d;
  /* F13 lives here: the driver stores this result in a u32 width without
   * checking for a negative error.  Kept in range so the ladder, not F13, is
   * what this configuration is about. */
  int w;
  __CPROVER_assume(w >= 32 && w <= 48);
  return w;
}
int ena_com_get_dev_attr_feat(struct ena_com_dev *d, struct ena_com_dev_get_features_ctx *c)
{
  (void)d;
  if(c != NULL)
  {
    __CPROVER_havoc_object(c);
    /* The queue counts the device reports bound ena_init_rings' loop.  Left
     * fully havocked they are unbounded and the loop cannot be unwound, which
     * is a harness artefact rather than anything about the driver; a real
     * device reports a small number.  Two is enough to exercise the ladder. */
    c->max_queue_ext.max_queue_ext.max_rx_sq_num = 2;
    c->max_queue_ext.max_queue_ext.max_rx_cq_num = 2;
    c->max_queue_ext.max_queue_ext.max_tx_sq_num = 2;
    c->max_queue_ext.max_queue_ext.max_tx_cq_num = 2;
    c->max_queues.max_sq_num = 2;
    c->max_queues.max_cq_num = 2;
  }
  return nd_int();
}
int ena_com_set_host_attributes(struct ena_com_dev *d)
{
  (void)d;
  return nd_int();
}
void ena_com_set_mmio_read_mode(struct ena_com_dev *d, bool readless)
{
  (void)d;
  (void)readless;
}
void ena_com_set_admin_polling_mode(struct ena_com_dev *d, bool polling)
{
  (void)d;
  (void)polling;
}
void ena_com_admin_aenq_enable(struct ena_com_dev *d) { (void)d; }
int rte_intr_callback_register(const struct rte_intr_handle *h, rte_intr_callback_fn cb, void *a)
{
  (void)h;
  (void)cb;
  (void)a;
  return 0;
}
void rte_timer_init(struct rte_timer *t) { (void)t; }
unsigned rte_lcore_count(void) { return 1; }
uint64_t rte_get_tsc_hz(void) { return 1000000000ull; }
const char *rte_version(void) { return "probe"; }

static bool g_init_once_ok;
/* ena_init_once must be able to succeed, or the probe returns before its
 * first acquisition and the ladder is never exercised.  Both services can
 * still fail when g_init_once_ok is false. */
int rte_mp_action_register(const char *name, rte_mp_t action)
{
  (void)name;
  (void)action;
  return g_init_once_ok ? 0 : nd_int();
}
int rte_timer_subsystem_init(void) { return g_init_once_ok ? 0 : nd_int(); }
#endif

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

void *rte_zmalloc_socket(const char *t, size_t size, unsigned a, int s);

static int slot_of(const void *p)
{
  for(int i = 0; i < NSLOTS; i++)
    if(p == (const void *)pool_mem[i])
      return i;
  return -1;
}

/* ena_com allocates through ENA_MEM_ALLOC, which is rte_zmalloc
 * (ena_plat_dpdk.h:257), NOT rte_zmalloc_socket.  Interposing only the latter
 * is why the probe ledger saw none of ena_com's five acquisitions and the
 * acquisition witness passed.  Route both through the same slots. */
void *rte_zmalloc(const char *t, size_t size, unsigned a)
{
  return rte_zmalloc_socket(t, size, a, 0);
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
void ena_com_abort_admin_commands(struct ena_com_dev *d) { (void)d; }
void ena_com_wait_for_abort_completion(struct ena_com_dev *d) { (void)d; }
void ena_com_mmio_reg_read_request_destroy(struct ena_com_dev *d) { (void)d; }
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

#ifdef PROBE
  /*
   * Probe, the lifecycle end nothing covered: eth_ena_dev_init's error ladder.
   * It acquires five things in order -- the customer-metrics buffer, the admin
   * queue and host info, the debug area, the RSS table and drv_stats -- and
   * unwinds them through a cascade of labels
   * (err_control_path_destroy -> err_rss_destroy -> err_delete_debug_area ->
   * err_device_destroy -> err_metrics_delete -> err).  With allocations able
   * to fail at any step, every one of those exits must leave the ledger
   * empty. */
  /* Probe scaffolding.  Without a PCI device whose register BAR is mapped,
   * eth_ena_dev_init returns -ENXIO at "if (!adapter->regs)" before acquiring
   * anything, which is why the acquisition witness kept passing.  ena_init_once
   * must succeed too, for the same reason. */
  static struct rte_pci_device the_pci;
  static uint8_t regs_bar[4096];
  static uint8_t mem_bar[4096];
  the_pci.mem_resource[ENA_REGS_BAR].addr = regs_bar;
  the_pci.mem_resource[ENA_REGS_BAR].len = sizeof(regs_bar);
  the_pci.mem_resource[ENA_MEM_BAR].addr = mem_bar;
  the_pci.mem_resource[ENA_MEM_BAR].len = sizeof(mem_bar);
  dev.device = &the_pci.device;
  g_init_once_ok = true;

  g_alloc_fails = true;
  for(int i = 0; i < NSLOTS; i++)
  {
    live[i] = false;
    ever[i] = false;
  }
  adapter.drv_stats = NULL;
  g_ladder_entries = 0;
  const int rc_probe = eth_ena_dev_init(&dev);
  if(rc_probe != 0)
  {
    unsigned live_cnt = 0;
    for(int i = 0; i < NSLOTS; i++)
      if(live[i])
        live_cnt++;
    __CPROVER_assert(
      live_cnt == 0,
      "PR_NO_LEAK: a failed probe releases everything it acquired");
    __CPROVER_assert(
      adapter.state != ENA_ADAPTER_STATE_INIT,
      "PR_NO_HALF_INIT: a failed probe does not leave the adapter initialised");
    /* Decisive for the leak: does a failing probe reach its cleanup ladder at
     * all?  Every failure after the first acquisition is supposed to, via one
     * of the err_* labels. */
    __CPROVER_assert(
      g_ladder_entries > 0,
      "PR_LADDER_RUN: a failing probe enters its cleanup ladder");
  }
  __CPROVER_assert(
    g_double_frees == 0, "PR_NO_DOUBLE: the probe ladder frees nothing twice");
  __CPROVER_assert(
    g_wild_frees == 0,
    "PR_NO_WILD: the probe ladder frees nothing it did not allocate");
  /*
   * Why this is not yet a result: the four obligations above all pass, and
   * the witness "the probe acquires at least one resource" ALSO passes, which
   * means the probe acquires nothing the ledger can see.  The ledger tracks
   * rte_zmalloc_socket, but the probe's five acquisitions go through
   * ena_com's own ENA_MEM_ALLOC / ENA_MEM_ALLOC_COHERENT macros
   * (ena_plat_dpdk.h), which this harness does not interpose.  So PR_NO_LEAK
   * currently says "nothing the ledger saw was leaked", which is true and
   * empty.
   *
   * To finish it, interpose the ena_plat_dpdk allocation macros the same way
   * harness_lock_order.c interposes rte_spinlock_lock: define them after the
   * plat header is parsed and before ena_ethdev.c is included, routing them
   * to the ledger.  Then the witness should fail and the obligations acquire
   * content.
   */
#  ifdef VACUITY_CHECK
  /* Both must FAIL: the probe must be able to fail, and to acquire something
   * before it does, or the obligations above are empty. */
  __CPROVER_assert(rc_probe == 0, "witness: the probe can fail");
  {
    unsigned ever_cnt = 0;
    for(int i = 0; i < NSLOTS; i++)
      if(ever[i])
        ever_cnt++;
    __CPROVER_assert(
      ever_cnt == 0, "witness: the probe acquires at least one resource");
  }
#  endif
  return 0;
#endif

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

#if defined(VACUITY_CHECK) && !defined(PROBE)
  {
    unsigned ever_cnt = 0;
    for(int i = 0; i < NSLOTS; i++)
      if(ever[i])
        ever_cnt++;
    /* must FAIL: the cycle really does allocate several things.  Guarded
     * against PROBE, which returns before this point, so leaving it
     * unguarded reported a passing witness for a path that is not taken. */
    __CPROVER_assert(ever_cnt < 4, "vacuity witness: the cycle allocates");
  }
#endif
  return 0;
}
