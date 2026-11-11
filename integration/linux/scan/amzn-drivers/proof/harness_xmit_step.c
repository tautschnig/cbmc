/*
 * The xmit step without a burst bound: a hand-written loop invariant for
 * eth_ena_xmit_pkts, machine-checked in two parts.
 *
 * eth_ena_xmit_pkts was proven only by unwinding its loop MAX_BURST times, so
 * every xmit result so far carries a burst restriction (1, or 2 in one
 * configuration).  Reducing the cost of the free-list predicate did not lift
 * it: ring 16 and 32 at burst 1 still fail to produce a verdict in 3600 s
 * (see the note in c2fb294de65), because xmit's cost is the
 * ena_xmit_mbuf -> ena_tx_map_mbuf path with a symbolic slot index, not the
 * predicate evaluation that dominated cleanup.
 *
 * The decomposition is simpler here than for cleanup, and for a specific
 * reason: ena_xmit_mbuf updates tx_ring->next_to_use itself, at the end, on
 * success (ena_ethdev.c:3192).  So the ring is in a consistent state at every
 * loop head, and the mid-loop invariant is just the full invariant -- no
 * generalised cursor is needed, unlike INV_CLEAN.
 *
 *   PART=1  STEP.  One loop-body iteration -- one ena_xmit_mbuf plus the
 *           pkts_without_db store that follows it -- from any state
 *           satisfying the invariant re-establishes the invariant, and
 *           transfers ownership of the packet to the slot its req_id names.
 *           Holding for one iteration, it holds for ANY burst length.
 *
 *   PART=2  EXIT.  The post-loop block -- the conditional doorbell and the
 *           available_desc update (ena_ethdev.c:3301-3310) -- preserves the
 *           invariant and establishes INV_wakeup, that no packet is left
 *           pushed without a doorbell.
 *
 * Together they give the xmit step for an unbounded burst at a fixed ring
 * size.  What is not machine-checked is the splice: that part 1 iterated and
 * then part 2 is the whole function.  The loop body is CALLED here rather
 * than transcribed -- ena_xmit_mbuf is a real function, unlike the cleanup
 * loop body -- so there is no transcription gap on this one.
 *
 * ena_com_prepare_tx is the stub whose contract harness_prepare_tx.c proves
 * against the real code, under ASSUME_NO_F14.  ASSUME_PREPARE_TX_OK makes it
 * always succeed, which is needed for the same reason the existing xmit step
 * needs it: on failure ena_xmit_mbuf leaves the packet in its slot (F12), so
 * the invariant genuinely does not hold on that path.
 *
 * Switches: RING, PART, VACUITY_CHECK.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING
#define RING 8
#endif
#ifndef PART
#define PART 1
#endif
#define SEG_LEN 64

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_adapter adapter;
static struct ena_ring ring;
static struct ena_tx_buffer pool_tbi[RING];
static uint16_t pool_etr[RING];
static uint8_t pool_push[128];
static struct ena_com_io_sq the_sq;
static struct ena_com_io_cq the_cq;
static volatile uint32_t the_db;

/* the packets already in flight, one per slot, pairwise distinct */
static struct rte_mbuf inflight[RING];
/* the one new packet this step submits */
static struct rte_mbuf newpkt;
static uint8_t newpkt_buf[SEG_LEN];

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

/*
 * ena_com_prepare_tx, stubbed to the contract harness_prepare_tx.c proves of
 * the real code: on success it writes at most num_bufs + 2 lines, at least
 * one for a non-empty packet, reports nb_hw_desc as the signed difference of
 * two u16 tails, and keeps the producer inside the queue.
 */
int ena_com_prepare_tx(
  struct ena_com_io_sq *io_sq,
  struct ena_com_tx_ctx *ctx,
  int *nb_hw_desc)
{
#ifndef ASSUME_PREPARE_TX_OK
  /* Without this the step fails XS_FREE_LIST and XS_DESC_SUM, and the trace
   * is F12: ena_tx_map_mbuf sets tx_info->mbuf before prepare_tx, so a
   * failure leaves the slot occupied while next_to_use is not advanced.  The
   * existing xmit step carries the same assumption for the same reason; it is
   * discharged by harness_prepare_tx.c under ASSUME_NO_F14. */
  if(nd_int())
    return ENA_COM_NO_MEM; /* the queue or the device refused */
#endif
  uint16_t descs = nd_u16();
  __CPROVER_assume(descs <= (uint16_t)ctx->num_bufs + 2);
  if(ctx->header_len != 0 || ctx->num_bufs != 0)
    __CPROVER_assume(descs >= 1);
  else
    __CPROVER_assume(descs >= 1); /* ASSUME_NONEMPTY_PKT: no empty packets (F15) */
  __CPROVER_assume(
    (uint16_t)(io_sq->tail + descs - io_sq->next_to_comp) <= io_sq->q_depth - 1);
  const uint16_t old = io_sq->tail;
  io_sq->tail = (uint16_t)(io_sq->tail + descs);
  *nb_hw_desc = (int)io_sq->tail - (int)old;
  return 0;
}

/* a well-formed single-segment mbuf, per DPDK's contract */
static struct rte_mbuf *make_pkt(void)
{
  newpkt.buf_addr = newpkt_buf;
  newpkt.buf_len = SEG_LEN;
  newpkt.data_off = nd_u16();
  newpkt.data_len = nd_u16();
  __CPROVER_assume(newpkt.data_off <= SEG_LEN);
  __CPROVER_assume(newpkt.data_len <= SEG_LEN - newpkt.data_off);
  __CPROVER_assume(newpkt.data_len >= 1); /* not an F15 empty packet */
  newpkt.buf_iova = 0x100000;
  newpkt.ol_flags = 0;
  newpkt.next = NULL;
  newpkt.nb_segs = 1;
  newpkt.pkt_len = newpkt.data_len;
  return &newpkt;
}

int main(void)
{
  unsigned i;

  __CPROVER_havoc_object(&the_sq.llq_info);
  ring.adapter = &adapter;
  ring.type = ENA_RING_TYPE_TX;
  ring.id = 0;
  ring.configured = 1;
  ring.ring_size = RING;
  ring.size_mask = RING - 1;
  ring.sgl_size = ENA_PKT_MAX_BUFS;
  ring.tx_buffer_info = pool_tbi;
  ring.empty_tx_reqs = pool_etr;
  ring.push_buf_intermediate_buf = pool_push;
  ring.tx_max_header_size = 96;
  ring.ena_com_io_sq = &the_sq;
  ring.ena_com_io_cq = &the_cq;
  ring.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  ring.tx_free_thresh = 0; /* the cleanup branch is a separate step */
  ring.offloads = 0;
  the_sq.mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  the_sq.q_depth = RING;
  the_sq.db_addr = (uint32_t *)&the_db;
  the_sq.tail = nd_u16();
  the_sq.next_to_comp = nd_u16();
  the_sq.llq_info.disable_meta_caching = false;
  adapter.state = ENA_ADAPTER_STATE_RUNNING;
  ring.next_to_use = nd_u16() & (RING - 1);
  ring.next_to_clean = nd_u16() & (RING - 1);
  ring.pkts_without_db = nd_u8() & 1;

  for(i = 0; i < RING; i++)
  {
    pool_tbi[i].mbuf = nd_int() ? &inflight[i] : NULL;
    pool_tbi[i].tx_descs = nd_u32();
    pool_tbi[i].num_of_bufs = nd_u16();
    __CPROVER_assume(pool_tbi[i].num_of_bufs <= ENA_PKT_MAX_BUFS);
    pool_etr[i] = nd_u16();
  }
  __CPROVER_assume(INV_COM(&the_sq));
  __CPROVER_assume(INV_STARTED(&ring));
  __CPROVER_assume(INV_FREE_LIST_FAST(&ring, RING));
  {
    uint16_t sum = 0;
    for(i = 0; i < RING; i++)
      if(pool_tbi[i].mbuf != NULL)
        sum = (uint16_t)(sum + TX_DESCS(pool_tbi[i]));
    __CPROVER_assume((uint16_t)(the_sq.tail - the_sq.next_to_comp) == sum);
  }

#if PART == 1
  /* ---- STEP: one loop-body iteration ---- */
  struct rte_mbuf *const pkt = make_pkt();
  /* the application's obligation: the packet is not already in flight.  It is
   * a distinct object from every inflight[] entry by construction. */
  const uint16_t ntu0 = ring.next_to_use;
  const uint16_t req0 = pool_etr[ntu0];
  const uint16_t probe = nd_u16() % RING;
  struct rte_mbuf *const held = pool_tbi[probe].mbuf;

  const int rc = ena_xmit_mbuf(&ring, pkt);
  if(rc == 0)
    ring.pkts_without_db = true; /* the loop body's second statement */

  /* ---- the invariant is re-established ---- */
  __CPROVER_assert(
    INV_STARTED(&ring), "XS_STARTED: one submit preserves INV_STARTED");
  __CPROVER_assert(
    INV_FREE_LIST_FAST(&ring, RING),
    "XS_FREE_LIST: one submit preserves INV_FREE_LIST");
  {
    uint16_t sum = 0;
    for(i = 0; i < RING; i++)
      if(pool_tbi[i].mbuf != NULL)
        sum = (uint16_t)(sum + TX_DESCS(pool_tbi[i]));
    __CPROVER_assert(
      (uint16_t)(the_sq.tail - the_sq.next_to_comp) == sum,
      "XS_DESC_SUM: one submit preserves INV_DESC_SUM");
  }
  if(rc == 0)
    __CPROVER_assert(
      pool_tbi[req0].mbuf == pkt,
      "XS_OWN_TRANSFER: an accepted packet is recorded in its req_id slot");
  if(held != NULL)
    __CPROVER_assert(
      pool_tbi[probe].mbuf == held,
      "XS_OWN_NO_LOSS: a packet in flight keeps its slot");
  __CPROVER_assert(
    rc != 0 || ring.pkts_without_db,
    "XS_DB_PENDING: an accepted packet leaves a doorbell owing");

#else
  /* ---- EXIT: the post-loop doorbell and stats block ---- */
  /* ena_ethdev.c:3301-3310, verbatim */
  if(likely(ring.pkts_without_db))
  {
    ena_com_write_sq_doorbell(ring.ena_com_io_sq);
    ring.tx_stats.doorbells++;
    ring.pkts_without_db = false;
  }
  ring.tx_stats.available_desc = ena_com_free_q_entries(ring.ena_com_io_sq);
  ring.tx_stats.tx_poll++;

  __CPROVER_assert(
    !ring.pkts_without_db,
    "XS_WAKEUP: after the exit block no packet is left pushed without a "
    "doorbell");
  __CPROVER_assert(
    INV_STARTED(&ring), "XS_EXIT_STARTED: the exit block preserves INV_STARTED");
  __CPROVER_assert(
    INV_FREE_LIST_FAST(&ring, RING),
    "XS_EXIT_FREE_LIST: the exit block preserves INV_FREE_LIST");
#endif

#ifdef VACUITY_CHECK
#  if PART == 1
  __CPROVER_assert(rc != 0, "vacuity witness: a packet can be accepted");
  __CPROVER_assert(rc == 0, "vacuity witness: a packet can be refused");
#  else
  __CPROVER_assert(
    !ring.tx_stats.doorbells, "vacuity witness: a doorbell can be rung");
#  endif
#endif
  return 0;
}
