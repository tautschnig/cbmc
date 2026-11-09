/*
 * RX base case: ena_rx_queue_setup establishes INV_RX_STARTABLE.
 *
 * The RX counterpart of harness_setup_basecase.c.  Together with the RX
 * teardown step (harness_rx_stop.c), which also establishes
 * INV_RX_STARTABLE, this gives ena_queue_start an entry predicate on both
 * paths -- first start and restart after stop or reset -- so the RX lifecycle
 * composes the same way the TX one does.
 *
 * Every allocation can fail, so the obligations cover the error paths too:
 * on failure the queue must not be left looking configured, or a later start
 * would run against a half-built queue.  Error paths are 61% of the Unveil
 * triggers.
 *
 *   RXSU_STARTABLE  on success, INV_RX_STARTABLE holds
 *   RXSU_FREELIST   on success, empty_rx_reqs is the identity permutation and
 *                   the indices are zeroed, which is what a fresh ring needs
 *   RXSU_ERRPATH    on failure, the queue is not left configured
 *
 * Switches: CONCRETE_RING, VACUITY_CHECK.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef CONCRETE_RING
#define CONCRETE_RING 4
#endif
#define RING CONCRETE_RING

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }

static struct ena_rx_buffer pool_rbi[RING];
static uint16_t pool_err[RING];
static struct rte_mbuf *pool_refill[RING];
static bool alloc_rbi_fail, alloc_err_fail, alloc_refill_fail;
static struct rte_mempool the_pool;
static uint16_t g_probe;

void *rte_zmalloc_socket(const char *t, size_t size, unsigned a, int s)
{
  (void)t;
  (void)a;
  (void)s;
  /* every allocation may fail: the error paths are part of the proof */
  if(size == sizeof(pool_rbi))
  {
    if(alloc_rbi_fail)
      return NULL;
    memset(pool_rbi, 0, sizeof(pool_rbi));
    return pool_rbi;
  }
  if(size == sizeof(pool_err))
  {
    if(alloc_err_fail)
      return NULL;
    memset(pool_err, 0, sizeof(pool_err));
    return pool_err;
  }
  if(size == sizeof(pool_refill))
  {
    if(alloc_refill_fail)
      return NULL;
    memset(pool_refill, 0, sizeof(pool_refill));
    return pool_refill;
  }
  return NULL;
}
/* the allocator hands out static pools, so the error paths must not free them */
void rte_free(void *p) { (void)p; }

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

/* ena_rx_queue_setup reads the mempool's element size */
uint16_t rte_pktmbuf_data_room_size(struct rte_mempool *mp)
{
  (void)mp;
  return 2048;
}

int main(void)
{
  static struct rte_eth_dev dev;
  static struct rte_eth_dev_data data;
  static struct ena_adapter adapter;
  static struct rte_eth_rxconf rx_conf;
  int rc;
  unsigned i;

  alloc_rbi_fail = nd_u16() & 1;
  alloc_err_fail = nd_u16() & 1;
  alloc_refill_fail = nd_u16() & 1;
  g_probe = nd_u16();

  dev.data = &data;
  data.dev_private = &adapter;
  data.port_id = 0;
  static void *rx_queues[1];
  data.rx_queues = rx_queues;
  data.nb_rx_queues = 1;

  adapter.max_rx_ring_size = nd_u16();
  __CPROVER_assume(adapter.max_rx_ring_size >= RING);
  /* ena_calc_io_queue_size clamps this; the bound is a device-init fact, as
   * for TX (proven by harness_llq_path.c -DDEVINIT_ONLY) */
  adapter.max_rx_sgl_size = nd_u16();
  __CPROVER_assume(
    adapter.max_rx_sgl_size >= 1 &&
    adapter.max_rx_sgl_size <= ENA_PKT_MAX_BUFS);

  /* queue 0, not yet configured */
  adapter.rx_ring[0].configured = 0;
  adapter.rx_ring[0].sgl_size = adapter.max_rx_sgl_size;

  rx_conf.rx_free_thresh = 0;
  rx_conf.offloads = 0;

  rc = ena_rx_queue_setup(&dev, 0, RING, 0, &rx_conf, &the_pool);

#if VACUITY_CHECK
  /* both must FAIL, or one outcome is unreachable and its obligation vacuous */
  __CPROVER_assert(rc != 0, "vacuity witness: an rx setup can succeed");
  __CPROVER_assert(rc == 0, "vacuity witness: an rx setup can fail");
#endif

  struct ena_ring *const r = &adapter.rx_ring[0];
  if(rc == 0)
  {
    __CPROVER_assert(
      INV_RX_STARTABLE(r, RING),
      "base case: ena_rx_queue_setup establishes INV_RX_STARTABLE");
    __CPROVER_assert(
      r->next_to_use == 0 && r->next_to_clean == 0,
      "base case: a fresh rx ring has zeroed producer and consumer indices");
    if(g_probe < RING)
    {
      uint16_t *const err = r->empty_rx_reqs;
      __CPROVER_assert(
        err[g_probe] == g_probe,
        "RXSU_FREELIST: empty_rx_reqs is the identity on a fresh ring");
    }
  }
  else
  {
    /* error path: nothing may be left looking usable */
    __CPROVER_assert(
      r->configured == 0,
      "error path: a failed rx setup leaves the queue unconfigured");
  }
  (void)i;
  return 0;
}
