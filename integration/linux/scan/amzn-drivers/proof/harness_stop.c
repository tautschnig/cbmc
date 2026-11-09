/*
 * STATUS: NOT ESTABLISHED.  This harness is written and reaches
 * ena_tx_queue_release_bufs, but no configuration tried makes it non-vacuous:
 * the vacuity witness "a stop can free a packet" does not FAIL, i.e. the
 * instrumented rte_pktmbuf_free is not observed to run on the watched mbuf.
 * The call in ena_tx_queue_release_bufs is to the `static inline`
 * rte_pktmbuf_free in rte_mbuf.h; the cleanup harness binds an identical
 * extern definition and is non-vacuous, so the two differ in a way not yet
 * pinned down (the pktmbuf_read separate-TU forwarding pattern is the likely
 * fix).  Do NOT read the three SUCCESS obligations here as proven: by the
 * vacuity witness they are about a release that frees nothing.  Left in the
 * tree as the starting point, not as a result.
 */
/*
 * The third operation of the induction: ena_queue_stop (teardown).
 *
 * This is the operation F12/F14 reach -- xmit's error path triggers a reset,
 * and the reset tears the queue down through ena_queue_stop ->
 * ena_tx_queue_release_bufs -> rte_pktmbuf_free.  Covering it as a standing
 * step turns the reset-frees-the-packet reasoning from a special-purpose
 * check into an obligation every state must satisfy, and it is Unveil class K
 * (unrecoverable / wrong state on teardown, 16%) plus more of class B.
 *
 * ena_queue_stop is NOT a loop over the ring the way cleanup is; it calls
 * ena_com_destroy_io_queue (device queue teardown) and then
 * ena_tx_queue_release_bufs, which frees every slot's mbuf.  The obligations:
 *
 *   FREE_ONCE         each in-flight mbuf is released exactly once
 *   STOP_NO_DANGLING  no slot still refers to a released mbuf afterwards
 *   STOP_UNCONFIGURED a stopped queue is no longer usable: configured is
 *                     cleared or the handles are gone, so a later xmit cannot
 *                     run against a torn-down queue
 *
 * Device trust: ena_com_destroy_io_queue frees DMA memory and touches no slot;
 * it is left as real code.  The mbuf free is instrumented, as in the cleanup
 * harness, to observe the discipline.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING_SIZE
#define RING_SIZE 8
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static int nd_int(void) { int v; return v; }

/* ghost: follow one arbitrary mbuf */
static struct rte_mbuf *g_watch;
static unsigned g_free_calls;
uint16_t g_probe;

static struct ena_adapter adapter;
static struct ena_ring ring;
static struct ena_tx_buffer pool_tbi[RING_SIZE];
static uint16_t pool_etr[RING_SIZE];
static uint8_t pool_push[96];
static struct rte_mbuf pool_mbuf[RING_SIZE];

void rte_pktmbuf_free(struct rte_mbuf *m)
{
  if(m == NULL)
    return;
  __CPROVER_assert(
    m != g_watch || g_free_calls == 0,
    "FREE_ONCE: no mbuf is released twice");
  if(m == g_watch)
    g_free_calls++;
}

/* ena_com_destroy_io_queue reaches the DMA free macros; model them as no-ops
 * on memory this harness does not track. */


int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
  (void)level;
  (void)logtype;
  (void)format;
#if VACUITY_CHECK
  /* must FAIL: a stop can free the watched mbuf, so the obligations are not
   * about a no-op teardown */
  __CPROVER_assert(g_free_calls == 0, "vacuity witness: a stop can free a packet");
#endif
  return 0;
}
int rte_log_register_type_and_pick_level(const char *name, uint32_t level_def)
{
  (void)name;
  (void)level_def;
  return 0;
}
void rte_pci_register(struct rte_pci_driver *driver) { (void)driver; }

static bool INV_FREE_LIST_LOCAL(const struct ena_ring *r)
{
  return INV_FREE_LIST(r, RING_SIZE);
}

int main(void)
{
  unsigned i;

  __CPROVER_havoc_object(&ring);
  __CPROVER_havoc_object(&adapter.ena_dev.io_sq_queues[0]);
  __CPROVER_havoc_object(&adapter.ena_dev.io_cq_queues[0]);

  ring.adapter = &adapter;
  ring.type = ENA_RING_TYPE_TX;
  ring.id = 0;
  ring.configured = 1;
  ring.ring_size = RING_SIZE;
  ring.size_mask = RING_SIZE - 1;
  ring.sgl_size = ENA_PKT_MAX_BUFS;
  ring.tx_buffer_info = pool_tbi;
  ring.empty_tx_reqs = pool_etr;
  ring.push_buf_intermediate_buf = pool_push;
  ring.ena_com_io_sq = &adapter.ena_dev.io_sq_queues[ENA_IO_TXQ_IDX(0)];
  ring.ena_com_io_cq = &adapter.ena_dev.io_cq_queues[ENA_IO_TXQ_IDX(0)];
  ring.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  ring.next_to_use = nd_u16() & (RING_SIZE - 1);
  ring.next_to_clean = nd_u16() & (RING_SIZE - 1);

  /* arbitrary occupancy, one distinct mbuf per slot */
  for(i = 0; i < RING_SIZE; i++)
  {
    pool_tbi[i].mbuf = nd_int() ? &pool_mbuf[i] : NULL;
    pool_tbi[i].num_of_bufs = nd_u16();
    __CPROVER_assume(pool_tbi[i].num_of_bufs <= ENA_PKT_MAX_BUFS);
    pool_tbi[i].tx_descs = nd_u16();
    __CPROVER_assume(
      pool_tbi[i].mbuf == NULL ||
      (pool_tbi[i].tx_descs >= 1 &&
       pool_tbi[i].tx_descs <= ENA_MAX_TX_DESCS_PER_PKT));
    pool_etr[i] = nd_u16();
    __CPROVER_assume(pool_etr[i] < RING_SIZE);
  }

  g_probe = nd_u16() % RING_SIZE;
  g_watch = &pool_mbuf[nd_u16() % RING_SIZE];
  g_free_calls = 0;

  /* entry state: a started, invariant-satisfying queue */
  __CPROVER_assume(INV_STARTED(&ring));
  __CPROVER_assume(INV_FREE_LIST_LOCAL(&ring));

  /* ---- one teardown operation ----
   * The mbuf-freeing half of ena_queue_stop.  ena_com_destroy_io_queue is the
   * other half (it frees the DMA rings and sends the device a destroy-queue
   * admin command); it touches no tx_buffer_info slot, so it is out of scope
   * for the ownership discipline and modelling its admin path would add the
   * untrusted-device machinery of harness_llq_path.c for no obligation. */
  ena_tx_queue_release_bufs(&ring);

  /* ---- obligations ---- */
  __CPROVER_assert(
    g_free_calls == 0 ||
      (g_probe >= RING_SIZE || ring.tx_buffer_info[g_probe].mbuf != g_watch),
    "STOP_NO_DANGLING: no slot refers to a released mbuf after stop");

  {
    bool any = false;
    for(i = 0; i < RING_SIZE; i++)
      if(ring.tx_buffer_info[i].mbuf != NULL)
        any = true;
    __CPROVER_assert(
      !any, "STOP_ALL_RELEASED: no slot still owns an mbuf after stop");
  }

  return 0;
}
