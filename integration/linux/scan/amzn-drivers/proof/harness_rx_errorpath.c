/*
 * RX error paths: the F10 shape on the RX side, and the interrupt-enable
 * operations on a queue that was never started.
 *
 * Two questions, each a call DPDK is entitled to make:
 *
 *  1. RELEASE_AFTER_FAILURE.  ena_rx_queue_setup can fail, and
 *     rte_eth_dev_close then reaches ena_rx_queue_release_all ->
 *     ena_rx_queue_release(dev, i) for every queue index, including one whose
 *     setup failed and whose dev->data->rx_queues[i] is therefore still NULL.
 *     ena_rx_queue_release dereferences it immediately
 *     (ena_ethdev.c:992-995) with no NULL check.  This is F10's shape on the
 *     RX side; F10 itself is the TX one (ena_tx_queue_release,
 *     ena_ethdev.c:1010).
 *
 *  2. INTR_ON_UNSTARTED.  ena_rx_queue_intr_enable/disable ->
 *     ena_rx_queue_intr_set (ena_ethdev.c:3880) takes
 *     &adapter->rx_ring[queue_id] and calls ena_com_unmask_intr, which writes
 *     through io_cq->unmask_reg.  Nothing checks that the queue was started,
 *     and unmask_reg is only set by ena_com_create_io_queue.  On a configured
 *     but unstarted queue it is whatever the allocation left, so the write
 *     goes to an unvalidated address.
 *
 * Both are error-path/ordering questions, which is 61% of the Unveil
 * triggers, and both are about what the ethdev layer is allowed to call next
 * rather than about a malfunctioning device.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"

#ifndef RING
#define RING 4
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_rx_buffer pool_rbi[RING];
static uint16_t pool_err[RING];
static struct rte_mbuf *pool_refill[RING];
static bool alloc_fail;
static struct rte_mempool the_pool;

void *rte_zmalloc_socket(const char *t, size_t size, unsigned a, int s)
{
  (void)t;
  (void)a;
  (void)s;
  if(alloc_fail)
    return NULL; /* the allocation the ethdev layer is entitled to see fail */
  if(size == sizeof(pool_rbi))
    return pool_rbi;
  if(size == sizeof(pool_err))
    return pool_err;
  if(size == sizeof(pool_refill))
    return pool_refill;
  return NULL;
}
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
  static void *rx_queues[1];
  int rc;

  dev.data = &data;
  data.dev_private = &adapter;
  data.port_id = 0;
  data.rx_queues = rx_queues;
  data.nb_rx_queues = 1;
  adapter.max_rx_ring_size = RING;
  adapter.max_rx_sgl_size = ENA_PKT_MAX_BUFS;
  adapter.rx_ring[0].configured = 0;
  adapter.rx_ring[0].sgl_size = ENA_PKT_MAX_BUFS;
  rx_conf.rx_free_thresh = 0;
  rx_conf.offloads = 0;

#if INTR_ON_UNSTARTED
  /* A queue that has been set up but never started: ena_queue_start, which is
   * the only thing that fills in ena_com_io_cq and its unmask_reg, has not
   * run.  rte_eth_dev_rx_intr_enable is callable in this state. */
  /* The adapter is a zeroed allocation at probe (rte_eth_dev_private is
   * zalloc'd), and ena_queue_start is the only thing that fills in
   * ena_com_io_cq.  Nothing here forces the field: it is left exactly as a
   * successful setup leaves it, and --pointer-check decides whether the
   * interrupt op can dereference it. */
  alloc_fail = false;
  rc = ena_rx_queue_setup(&dev, 0, RING, 0, &rx_conf, &the_pool);
  __CPROVER_assume(rc == 0);
  ena_rx_queue_intr_enable(&dev, 0);
#else
  /* The setup fails, so rx_queues[0] is never published, and close() then
   * releases every queue index. */
  alloc_fail = true;
  rc = ena_rx_queue_setup(&dev, 0, RING, 0, &rx_conf, &the_pool);
  __CPROVER_assume(rc != 0);
  __CPROVER_assert(
    rx_queues[0] != NULL,
    "RX_RELEASE_NONNULL: the queue released after a failed setup is non-NULL");
#  if RELEASE_AFTER_FAILURE
  /* the call rte_eth_dev_close is entitled to make next */
  ena_rx_queue_release_all(&dev);
#  endif
#endif
  return 0;
}
