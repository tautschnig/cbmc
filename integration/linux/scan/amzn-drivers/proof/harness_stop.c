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
 *   FREE_ONCE                    no mbuf is released twice
 *   STOP_FREES_EXACTLY_INFLIGHT  an mbuf is released iff it was in flight
 *   STOP_FREE_COUNT              releases == in-flight count
 *   STOP_NO_DANGLING             no slot refers to an mbuf afterwards
 *   STOP_RESTARTABLE             post-state is what ena_queue_start needs
 *
 * "In flight" is "occupies a slot": under INV_FREE_LIST the occupied slots
 * are exactly the in-flight packets.  F12 is precisely a state that breaks
 * that (a slot keeps an mbuf the application was told was not sent), so
 * F12/F14 shows up here as the pre-state obligation INV_FREE_LIST, which the
 * xmit step must re-establish -- and does only under the F14 device
 * precondition.
 *
 * Scope: ena_tx_queue_release_bufs only.  ena_com_destroy_io_queue writes no
 * tx_buffer_info slot; it is not part of this step.  The mbuf free is
 * instrumented to observe the discipline.
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
static unsigned g_total_frees;
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
  g_total_frees++;
}


int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
  (void)level;
  (void)logtype;
  (void)format;
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
    pool_tbi[i].tx_descs = nd_u32(); /* any unsigned, read mod 2^16 */
    pool_etr[i] = nd_u16();
    __CPROVER_assume(pool_etr[i] < RING_SIZE);
  }

  g_probe = nd_u16() % RING_SIZE;
  const uint16_t w = nd_u16() % RING_SIZE;
  g_watch = &pool_mbuf[w];
  g_free_calls = 0;
  g_total_frees = 0;

  /* entry state: a started, invariant-satisfying queue */
  __CPROVER_assume(INV_STARTED(&ring));
  __CPROVER_assume(INV_FREE_LIST_LOCAL(&ring));

  const bool watched_in_flight = pool_tbi[w].mbuf != NULL;
  const uint16_t inflight =
    (uint16_t)((ring.next_to_use - ring.next_to_clean) & ring.size_mask);

  /* ---- one teardown operation ----
   * The mbuf-freeing half of ena_queue_stop.  ena_com_destroy_io_queue is the
   * other half: it frees the DMA rings and sends a destroy-queue admin
   * command, and writes no tx_buffer_info slot, so the ownership obligations
   * below do not depend on it. */
  ena_tx_queue_release_bufs(&ring);

  /* ---- obligations ---- */
  __CPROVER_assert(
    g_free_calls == (watched_in_flight ? 1u : 0u),
    "STOP_FREES_EXACTLY_INFLIGHT: an mbuf is released iff it was in flight, "
    "and then exactly once");
  __CPROVER_assert(
    g_total_frees == inflight,
    "STOP_FREE_COUNT: the number of releases equals the in-flight count");
  __CPROVER_assert(
    ring.tx_buffer_info[g_probe].mbuf == NULL,
    "STOP_NO_DANGLING: no slot refers to an mbuf after stop");
  /* restart: what ena_queue_start assumes (shared with the setup base case) */
  __CPROVER_assert(
    INV_STARTABLE(&ring, RING_SIZE),
    "STOP_RESTARTABLE: ena_tx_queue_release_bufs establishes INV_STARTABLE");

#if VACUITY_CHECK
  /* both must FAIL */
  __CPROVER_assert(g_free_calls == 0, "vacuity witness: a stop can free the watched packet");
  __CPROVER_assert(inflight < 2, "vacuity witness: a stop can face two in-flight packets");
#endif
  return 0;
}
