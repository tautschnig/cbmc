/*
 * RX teardown step: ena_rx_queue_release_bufs, the buffer-freeing half of
 * ena_queue_stop for an RX queue.  The RX counterpart of harness_stop.c.
 *
 * This is where Unveil classes B (lifetime: double free, use after free) and
 * K (wrong state on teardown) sit for the RX side, and it closes the RX
 * lifecycle loop: a stopped RX queue must be startable again.
 *
 * Obligations:
 *   RXS_FREE_ONCE     no buffer is released twice
 *   RXS_FREES_POSTED  a buffer is released iff its slot held one
 *   RXS_FREE_COUNT    the number of releases equals the number of posted slots
 *   RXS_NO_DANGLING   no slot refers to a buffer afterwards
 *   RXS_RESTARTABLE   INV_RX_STARTABLE holds, which is what ena_queue_start
 *                     assumes
 *
 * Scope: ena_rx_queue_release_bufs only.  ena_com_destroy_io_queue is the
 * other half of ena_queue_stop; it writes no rx_buffer_info slot, so the
 * ownership obligations here do not depend on it (same split as the TX stop
 * step).
 *
 * The RX path frees with rte_mbuf_raw_free, which goes to rte_mempool_put;
 * that is the function instrumented here.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING_SIZE
#define RING_SIZE 8
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static int nd_int(void) { int v; return v; }

/* ghost: follow one arbitrary buffer */
static struct rte_mbuf *g_watch;
static unsigned g_free_calls;
static unsigned g_total_frees;
uint16_t g_probe;

static struct ena_adapter adapter;
static struct ena_ring ring;
static struct ena_rx_buffer pool_rbi[RING_SIZE];
static uint16_t pool_err[RING_SIZE];
static struct rte_mbuf *pool_refill[RING_SIZE];
static struct rte_mbuf pool_mbuf[RING_SIZE];
static struct rte_mempool the_pool;

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

/* rte_mbuf_raw_free's destination */
void rte_mempool_put(struct rte_mempool *mp, void *obj)
{
  (void)mp;
  if(obj == NULL)
    return;
  __CPROVER_assert(
    obj != (void *)g_watch || g_free_calls == 0,
    "RXS_FREE_ONCE: no buffer is released twice");
  if(obj == (void *)g_watch)
    g_free_calls++;
  g_total_frees++;
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
  ring.ena_com_io_sq = sq;
  ring.ena_com_io_cq = cq;
  sq->q_depth = RING_SIZE;
  sq->direction = ENA_COM_IO_QUEUE_DIRECTION_RX;
  sq->mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;
  ring.next_to_use = nd_u16() & (RING_SIZE - 1);
  ring.next_to_clean = nd_u16() & (RING_SIZE - 1);

  /* arbitrary occupancy, one distinct buffer per slot */
  for(i = 0; i < RING_SIZE; i++)
  {
    pool_rbi[i].mbuf = nd_int() ? &pool_mbuf[i] : NULL;
    pool_mbuf[i].pool = &the_pool;
    pool_mbuf[i].refcnt = 1;
    pool_err[i] = nd_u16();
    __CPROVER_assume(pool_err[i] < RING_SIZE);
  }

  g_probe = nd_u16() % RING_SIZE;
  const uint16_t w = nd_u16() % RING_SIZE;
  g_watch = &pool_mbuf[w];
  g_free_calls = 0;
  g_total_frees = 0;

  __CPROVER_assume(INV_RX_STARTED(&ring, RING_SIZE));

  const bool watched_posted = pool_rbi[w].mbuf != NULL;
  unsigned posted = 0;
  for(i = 0; i < RING_SIZE; i++)
    if(pool_rbi[i].mbuf != NULL)
      posted++;

  /* ---- one teardown ---- */
  ena_rx_queue_release_bufs(&ring);

  /* ---- obligations ---- */
  __CPROVER_assert(
    g_free_calls == (watched_posted ? 1u : 0u),
    "RXS_FREES_POSTED: a buffer is released iff its slot held one, and then "
    "exactly once");
  __CPROVER_assert(
    g_total_frees == posted,
    "RXS_FREE_COUNT: the number of releases equals the number of posted slots");
  __CPROVER_assert(
    ring.rx_buffer_info[g_probe].mbuf == NULL,
    "RXS_NO_DANGLING: no slot refers to a buffer after release");
  __CPROVER_assert(
    INV_RX_STARTABLE(&ring, RING_SIZE),
    "RXS_RESTARTABLE: release establishes INV_RX_STARTABLE");

#if VACUITY_CHECK
  /* both must FAIL */
  __CPROVER_assert(
    g_free_calls == 0, "vacuity witness: a release can free the watched buffer");
  __CPROVER_assert(
    posted < 2, "vacuity witness: a release can face two posted buffers");
#endif
  return 0;
}
