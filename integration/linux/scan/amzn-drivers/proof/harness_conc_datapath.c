/*
 * Concurrency pair 3: the TX datapath against a control-path stop.
 *
 * The first two pairs (harness_conc_reset.c, harness_conc_admin.c) abstract
 * the shared state into a local model, because CBMC's thread support does not
 * scale to this driver.  This pair does not need to: the control side splits
 * naturally into two driver calls, so the REAL ring, the REAL
 * ena_tx_queue_release_bufs and the REAL ena_xmit_mbuf can be interleaved by
 * hand.
 *
 * ena_queue_stop does two things in this order (ena_ethdev.c:1474-1485):
 *
 *     ena_com_destroy_io_queue(ena_dev, ENA_IO_TXQ_IDX(ring->id));
 *     ena_tx_queue_release_bufs(ring);
 *
 * so there is a window in which the io queue is gone but the ring still owns
 * the application's mbufs.  A transmit that lands in that window submits to a
 * destroyed queue; one that lands after it writes into slots the stop has
 * already emptied.
 *
 * **The driver has a check for exactly this, and it is compiled out.**
 * eth_ena_xmit_pkts opens with
 *
 *     #ifdef RTE_ETHDEV_DEBUG_TX
 *             if (unlikely(tx_ring->adapter->state != ENA_ADAPTER_STATE_RUNNING))
 *                     return 0;
 *     #endif
 *
 * and RTE_ETHDEV_DEBUG_TX is not set in a normal build -- the same shape as
 * F16's compiled-out RTE_ASSERT.  So the question this harness answers is
 * what that absent check is worth: with it, the datapath declines; without
 * it, these obligations say what happens instead.
 *
 * DPDK's threading model makes it the application's job not to transmit on a
 * queue it is stopping, so a failure here is an application-contract
 * violation rather than a driver defect -- the same standing as F15, F22 and
 * F24.  What is worth knowing is which obligation breaks first, because that
 * is what the compiled-out check protects.
 *
 *   CC3_NO_SUBMIT_AFTER_DESTROY  no descriptor is submitted to an io queue
 *                                that stop has destroyed
 *   CC3_NO_DOUBLE_FREE           no mbuf is released twice
 *   CC3_NO_LOST_MBUF             an mbuf the driver accepted is either still
 *                                owned by a slot or has been freed, never
 *                                neither
 *
 * -DWITH_STATE_CHECK compiles the state check in, i.e. defines
 *  RTE_ETHDEV_DEBUG_TX behaviour at the harness level, so the two
 *  configurations say what the check buys.
 *
 * Switches: RING, WHEN (0,1,2 -- where the transmit lands), VACUITY_CHECK,
 * WITH_STATE_CHECK.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING
#define RING 4
#endif
#ifndef WHEN
#define WHEN 1
#endif
#define SEG_LEN 128

static int nd_int(void) { int v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }

static struct ena_adapter adapter;
static struct rte_eth_dev_data data;
static struct ena_ring ring;
static struct ena_tx_buffer pool_tbi[RING];
static uint16_t pool_etr[RING];
static struct ena_com_io_sq the_sq;
static struct ena_com_io_cq the_cq;
static uint32_t the_db;
static uint8_t pool_push[RING * SEG_LEN];
static struct rte_mbuf newpkt;
static uint8_t newpkt_buf[SEG_LEN];

/* ---- ghost state ---- */
static bool g_io_destroyed;   /* ena_com_destroy_io_queue has run */
static unsigned g_submits_after_destroy;
static unsigned g_free_count[RING + 1]; /* per watched mbuf; index RING = the new one */
static bool g_accepted;       /* xmit took ownership of the new mbuf */
static bool g_attempted;      /* the datapath ran at all */

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

static int slot_of(const struct rte_mbuf *m)
{
  if(m == &newpkt)
    return RING;
  return -1;
}
void rte_pktmbuf_free(struct rte_mbuf *m)
{
  const int s = slot_of(m);
  if(s >= 0)
  {
    __CPROVER_assert(
      g_free_count[s] == 0, "CC3_NO_DOUBLE_FREE: no mbuf is released twice");
    g_free_count[s]++;
  }
}
void rte_pktmbuf_free_bulk(struct rte_mbuf **m, unsigned int n)
{
  for(unsigned k = 0; k < n; k++)
    rte_pktmbuf_free(m[k]);
}

/* The submit path, modelled so that a submission after the io queue has been
 * destroyed is visible rather than merely undefined. */
int ena_com_prepare_tx(
  struct ena_com_io_sq *io_sq, struct ena_com_tx_ctx *ctx, uint16_t *nb_hw_desc)
{
  (void)ctx;
  if(g_io_destroyed)
    g_submits_after_destroy++;
  __CPROVER_assert(
    !g_io_destroyed,
    "CC3_NO_SUBMIT_AFTER_DESTROY: no descriptor is submitted to an io queue "
    "that stop has destroyed");
  *nb_hw_desc = 1;
  io_sq->tail = (uint16_t)(io_sq->tail + 1);
  return 0;
}
void ena_com_destroy_io_queue(struct ena_com_dev *d, uint16_t qid)
{
  (void)d;
  (void)qid;
  /* Faithful to ena_com_io_queue_free: it frees the DMA rings and NULLs the
   * virtual addresses, and does NOT zero q_depth or llq_info.  An earlier
   * version of this model zeroed q_depth, which invented two consequences the
   * real code does not have -- an underflow in ena_com_free_q_entries and a
   * division by zero in ena_com_sq_have_enough_space.  Modelling a teardown
   * as more destructive than it is manufactures findings. */
  the_sq.desc_addr.virt_addr = NULL;
  the_cq.cdesc_addr.virt_addr = NULL;
  g_io_destroyed = true;
}

static void datapath_step(void)
{
  g_attempted = true;
#ifdef WITH_STATE_CHECK
  /* what RTE_ETHDEV_DEBUG_TX would compile in */
  if(adapter.state != ENA_ADAPTER_STATE_RUNNING)
    return;
#endif
  if(ena_xmit_mbuf(&ring, &newpkt) == 0)
    g_accepted = true;
}

int main(void)
{
  unsigned i;

  ring.adapter = &adapter;
  adapter.edev_data = &data;
  ring.id = 0;
  ring.type = ENA_RING_TYPE_TX;
  ring.ring_size = RING;
  ring.size_mask = RING - 1;
  ring.tx_buffer_info = pool_tbi;
  ring.empty_tx_reqs = pool_etr;
  ring.push_buf_intermediate_buf = pool_push;
  ring.tx_max_header_size = 96;
  ring.ena_com_io_sq = &the_sq;
  ring.ena_com_io_cq = &the_cq;
  ring.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  ring.tx_free_thresh = 0;
  ring.offloads = 0;
  ring.sgl_size = 2;
  the_sq.mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  the_sq.q_depth = RING;
  the_sq.db_addr = &the_db;
  the_sq.tail = nd_u16();
  the_sq.next_to_comp = the_sq.tail;
  the_sq.llq_info.disable_meta_caching = false;
  /* the LLQ geometry survives a destroy, so it must be set here or the
   * divisor in ena_com_sq_have_enough_space is a static zero */
  the_sq.llq_info.descs_per_entry = 8;
  the_sq.llq_info.desc_list_entry_size = 128;
  the_sq.llq_info.descs_num_before_header = 1;
  the_sq.desc_entry_size = 16;
  the_sq.tx_max_header_size = 96;
  the_sq.desc_addr.virt_addr = pool_push;
  adapter.state = ENA_ADAPTER_STATE_RUNNING;

  ring.next_to_use = nd_u16() & (RING - 1);
  ring.next_to_clean = ring.next_to_use;
  for(i = 0; i < RING; i++)
  {
    pool_tbi[i].mbuf = NULL; /* an empty ring, so the only mbuf is the new one */
    pool_tbi[i].tx_descs = 0;
    pool_tbi[i].num_of_bufs = 0;
    pool_etr[i] = (uint16_t)i;
    g_free_count[i] = 0;
  }
  g_free_count[RING] = 0;
  g_io_destroyed = false;
  g_submits_after_destroy = 0;
  g_accepted = false;
  g_attempted = false;

  /* a well-formed single-segment packet */
  newpkt.buf_addr = newpkt_buf;
  newpkt.buf_len = SEG_LEN;
  newpkt.data_off = 0;
  newpkt.data_len = 64;
  newpkt.pkt_len = 64;
  newpkt.nb_segs = 1;
  newpkt.next = NULL;
  newpkt.ol_flags = 0;
  newpkt.buf_iova = 0x100000;

  /* ---- the interleaving ----
   *
   * The control side's order is fixed by the driver; the datapath lands at
   * one of the three points around it.  WHEN selects which, so each is a
   * separate verification rather than a disjunction inside one.
   *
   * adapter->state stays RUNNING across both teardown steps, which is
   * faithful to ena_stop: it destroys the queues first and assigns
   * ENA_ADAPTER_STATE_STOPPED nineteen lines later (ena_ethdev.c:1379-1406).
   * ena_start does the opposite and is safe for it -- it starts the queues and
   * only then assigns RUNNING (:1323-1347) -- so the convention exists and the
   * stop path departs from it. */
#ifdef WITH_EARLY_STATE
  /* The fix: leave RUNNING before tearing anything down, so a concurrent
   * datapath call that checks the state declines.  Needs the state check to
   * be compiled in as well, which is what pairing this with
   * WITH_STATE_CHECK tests. */
  adapter.state = ENA_ADAPTER_STATE_STOPPED;
#endif
#if WHEN == 0
  datapath_step();
  ena_com_destroy_io_queue(&adapter.ena_dev, ENA_IO_TXQ_IDX(ring.id));
  ena_tx_queue_release_bufs(&ring);
#elif WHEN == 1
  ena_com_destroy_io_queue(&adapter.ena_dev, ENA_IO_TXQ_IDX(ring.id));
  datapath_step();
  ena_tx_queue_release_bufs(&ring);
#else
  ena_com_destroy_io_queue(&adapter.ena_dev, ENA_IO_TXQ_IDX(ring.id));
  ena_tx_queue_release_bufs(&ring);
  datapath_step();
#endif

  /* ---- the obligation that outlives the interleaving ----
   *
   * If the driver took the packet, it must still be accounted for: either a
   * slot owns it, or it has been freed.  Neither means the application's mbuf
   * has been dropped on the floor. */
  if(g_accepted)
  {
    bool owned = false;
    for(i = 0; i < RING; i++)
      if(pool_tbi[i].mbuf == &newpkt)
        owned = true;
    __CPROVER_assert(
      owned || g_free_count[RING] == 1,
      "CC3_NO_LOST_MBUF: an accepted mbuf is either owned by a slot or freed");
  }

#ifdef VACUITY_CHECK
  /* Must FAIL: the datapath really runs, or nothing below is about anything. */
  __CPROVER_assert(!g_attempted, "witness: the datapath is attempted");
#  if WHEN != 0
  __CPROVER_assert(
    !g_io_destroyed, "witness: the io queue is destroyed before the transmit");
#  endif
#  if defined(WITH_EARLY_STATE) && defined(WITH_STATE_CHECK)
  /* With the fix the transmit must be DECLINED, so witnessing that it can be
   * accepted would be wrong.  Witness the decline instead: the obligation
   * above holds because nothing is submitted, and that is the intended
   * behaviour rather than a vacuous pass. */
  __CPROVER_assert(g_accepted, "witness: the transmit is declined");
#  else
  __CPROVER_assert(!g_accepted, "witness: the transmit can be accepted");
#  endif
#endif
  return 0;
}
