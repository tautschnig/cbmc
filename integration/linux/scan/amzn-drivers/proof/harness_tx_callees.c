/*
 * Stage 1 of doc/cbmc-driver-proof-design-2026-11.md: discharge the callee
 * contracts.  Unlike harness_tx_allseq.c, the real bodies of ena_xmit_mbuf,
 * ena_tx_map_mbuf and ena_tx_cleanup are kept; the assumption boundary moves
 * out to the DPDK mbuf API, which is where it belongs.
 *
 * That forces two further specifications to be written down:
 *   INV_RING  -- the per-queue invariant, now including the req_id indirection
 *                (empty_tx_reqs entries index tx_buffer_info)
 *   WF_MBUF   -- DPDK's mbuf contract: nb_segs equals the ->next chain
 *                length, each segment's data lies inside its buffer
 *
 * VACUITY_CHECK=1 turns the harness into its own vacuity witness: it asserts
 * sent == 0, which is EXPECTED TO FAIL.  If it succeeds, no packet is ever
 * accepted and every obligation about the accept path is vacuous.  This
 * matters because ena_xmit_mbuf requires
 * ena_com_free_q_entries(io_sq) >= nb_segs + 2 and free <= ring_size - 1, so a
 * k-segment packet needs RING_SIZE >= k + 3.  At RING_SIZE=2 nothing is ever
 * accepted and the proof is empty.
 *
 * SEGS_ASSUMED selects which API contract we assume the application honours:
 *   1 = the app ran rte_eth_tx_prepare (or otherwise respects the advertised
 *       tx_max_num_segs), so nb_segs <= sgl_size
 *   0 = the app called rte_eth_tx_burst directly, which DPDK permits
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef MAX_BURST
#define MAX_BURST 2
#endif
#ifndef MAX_SEGS
#define MAX_SEGS 4
#endif
#ifndef RING_SIZE
#define RING_SIZE 8            /* a power of 2; geometry, not the bound of interest */
#endif
#ifndef SEGS_ASSUMED
#define SEGS_ASSUMED 1
#endif
#ifndef SLOTS_BY_LOOP
#  define SLOTS_BY_LOOP 0
#endif
#ifndef VACUITY_CHECK
#  define VACUITY_CHECK 0
#endif
#if RING_SIZE < MAX_SEGS + 3
#  warning                                                                     \
    "RING_SIZE < MAX_SEGS + 3: packets with MAX_SEGS segments can never be accepted (see VACUITY_CHECK)"
#endif
#define SEG_BUF_LEN 64

/* Ownership probes: two arbitrary slot indices, fixed before the call and
 * used in both the assumed and the asserted form of the ownership invariant.
 * Proving the two-slot instance for nondeterministic indices is universal
 * quantification over every pair of slots. */
uint16_t g_p1, g_p2;

/* nondet via uninitialised locals: a bodyless function would be flagged
 * "undefined function should be unreachable" by the DFCC instrumentation */
static uint16_t nd_u16(void);

/*
 * ena_com_prepare_tx lives in ena_eth_com.c, which is not compiled in, so it
 * is bodyless -- and under DFCC a bodyless callee becomes an unreachable
 * stub, which kills every path through it.  That, not a weak invariant, is
 * what made the accept path unreachable and the accept-path obligations
 * vacuous: the only way out of the TX loop was ena_xmit_mbuf's error break.
 *
 * The stub states the contract the driver relies on: on success it consumes
 * at most the number of descriptors ena_xmit_mbuf checked space for
 * (num_bufs + 2, see ena_ethdev.c:3140) and advances the SQ producer by that
 * much.  Keeping to that bound is what preserves the SQ accounting clause of
 * INV; a stub that advanced further would (correctly) break it.
 */
static bool g_prepare_tx_failed;

int ena_com_prepare_tx(
  struct ena_com_io_sq *io_sq,
  struct ena_com_tx_ctx *ena_tx_ctx,
  int *nb_hw_desc)
{
  int rc;
  uint16_t descs;

  rc = (int)nd_u16();
#ifdef ASSUME_PREPARE_TX_OK
  /* Masks F12: when ena_com_prepare_tx fails, ena_xmit_mbuf leaves
   * tx_info->mbuf pointing at a packet it then reports as unsent, and the
   * reset it triggers frees that packet in ena_tx_queue_release_bufs.  The
   * slot invariants (INV_SLOT, INV_DESC_SUM) are violated on exactly that
   * path, so they are claimed only for a prepare_tx that succeeds. */
  rc = 0;
#endif
  if (rc != 0)
  {
    g_prepare_tx_failed = true;
    return rc;                      /* the device/queue rejected the request */
  }

  /* The contract harness_prepare_tx.c proves for the real ena_eth_com.c
   * (ddf9fc11956): it writes at most num_bufs + 2 lines, at least one unless
   * the packet is empty (header_len == 0 and num_bufs == 0), stays inside the
   * queue, and reports nb_hw_desc = (int)new_tail - (int)old_tail, which is
   * negative when the u16 tail wraps. */
  descs = nd_u16();
  __CPROVER_assume(descs <= (uint16_t)ena_tx_ctx->num_bufs + 2);
#ifdef ASSUME_NONEMPTY_PKT
  /* application obligation: no zero-length packets */
  __CPROVER_assume(ena_tx_ctx->header_len != 0 || ena_tx_ctx->num_bufs != 0);
#endif
  if(ena_tx_ctx->header_len != 0 || ena_tx_ctx->num_bufs != 0)
    __CPROVER_assume(descs >= 1);
  __CPROVER_assume(
    (uint16_t)(io_sq->tail + descs - io_sq->next_to_comp) <= io_sq->q_depth - 1);
  {
    const uint16_t old_tail = io_sq->tail;
    io_sq->tail = (uint16_t)(io_sq->tail + descs);
    *nb_hw_desc = (int)io_sq->tail - (int)old_tail;
  }
  return 0;
}

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }

/* mbuf pool, defined ahead of INV_RING because the invariant refers to it */
static struct rte_mbuf segs[MAX_SEGS];
static uint8_t seg_bufs[MAX_SEGS][SEG_BUF_LEN];

#if DISTINCT_MBUFS
/* One mbuf chain per packet of the burst, and a separate pool standing for
 * the packets already in flight in each slot.  New packets are therefore
 * distinct from each other and from everything in flight by construction --
 * which is the application's obligation (finding 2) stated exactly, instead of
 * assumed at two probe indices. */
static struct rte_mbuf pkt_segs[MAX_BURST][MAX_SEGS];
static uint8_t pkt_bufs[MAX_BURST][MAX_SEGS][SEG_BUF_LEN];
static struct rte_mbuf inflight_pool[RING_SIZE];

static bool IS_NEW_PKT(const struct rte_mbuf *m)
{
  for(unsigned b = 0; b < MAX_BURST; b++)
    if(m == &pkt_segs[b][0])
      return true;
  return false;
}
#  define SLOT_MBUF_OK(m, p)                                                   \
    ((m) == NULL || (m) == &inflight_pool[(p)] || IS_NEW_PKT(m))
#  define SLOT_MBUF_PRE(m, p) ((m) == NULL || (m) == &inflight_pool[(p)])
#else
#  define SLOT_MBUF_OK(m, p) ((m) == NULL || (m) == &segs[0])
#  define SLOT_MBUF_PRE(m, p) ((m) == NULL || (m) == &segs[0])
#endif

/* ---------------- INV: what a valid ENA TX queue is ---------------- */

/*
 * OWN_UNIQUE: the first clause of the ownership map.  A tx_buffer_info slot
 * owns at most one mbuf and no mbuf is owned by two slots, so an accepted
 * packet can never be freed twice and never silently dropped by being
 * overwritten in place.  This is the class that the taint pipeline cannot
 * express at all and that dominates the Unveil findings (UAF, double-free,
 * leak = 25%).
 */
static bool OWN_UNIQUE(const struct ena_ring *r, uint16_t p1, uint16_t p2)
{
	struct ena_tx_buffer *tbi = r->tx_buffer_info;

	if (p1 >= r->ring_size || p2 >= r->ring_size || p1 == p2)
		return true;
	return !(tbi[p1].mbuf != NULL && tbi[p1].mbuf == tbi[p2].mbuf);
}

/*
 * The per-slot invariant, stated for one slot.  Asserting it at a
 * nondeterministic index is universal quantification over slots; assuming it
 * needs the real quantifier (INV_SLOTS_ASSUME), because a hypothesis cannot
 * be instantiated at a single point and then used elsewhere.
 */
static bool INV_SLOT(const struct ena_ring *r, uint16_t p)
{
  uint16_t *etr = r->empty_tx_reqs;
  struct ena_tx_buffer *tbi = r->tx_buffer_info;

  if(p >= r->ring_size)
    return true;
  return etr[p] < r->ring_size &&
         SLOT_MBUF_OK(tbi[p].mbuf, p) &&
         tbi[p].num_of_bufs <= ENA_PKT_MAX_BUFS &&
         (tbi[p].mbuf == NULL || TX_DESCS(tbi[p]) <= ENA_MAX_TX_DESCS_PER_PKT);
}

/* The same clauses as a quantified assumption over every slot.  Written over
 * the harness's own arrays so it does not go through the anonymous-union
 * fields.  __CPROVER_forall needs a side-effect-free expression, hence the
 * inlined body rather than a call to INV_SLOT. */
#if SLOTS_BY_LOOP
/* Explicit loop form, for measuring the cost of the quantifier against an
 * unrolled conjunction at small ring sizes. */
#  define INV_SLOTS_ASSUME(etr, tbi)                                           \
    do                                                                         \
    {                                                                          \
      for(unsigned _i = 0; _i < RING_SIZE; _i++)                               \
      {                                                                        \
        __CPROVER_assume((etr)[_i] < RING_SIZE);                               \
        __CPROVER_assume(SLOT_MBUF_PRE((tbi)[_i].mbuf, _i));                 \
        __CPROVER_assume((tbi)[_i].num_of_bufs <= ENA_PKT_MAX_BUFS);            \
        __CPROVER_assume(                                                      \
          (tbi)[_i].mbuf == 0 ||                                               \
          TX_DESCS((tbi)[_i]) <= ENA_MAX_TX_DESCS_PER_PKT);                     \
      }                                                                        \
    } while(0)
#else
#  define INV_SLOTS_ASSUME(etr, tbi)                                           \
    __CPROVER_assume(__CPROVER_forall {                                        \
      unsigned _i;                                                             \
      _i < RING_SIZE ==> ((etr)[_i] < RING_SIZE &&                             \
                          SLOT_MBUF_PRE((tbi)[_i].mbuf, _i) &&               \
                          (tbi)[_i].num_of_bufs <= ENA_PKT_MAX_BUFS &&         \
                          ((tbi)[_i].mbuf == 0 ||                              \
                           TX_DESCS((tbi)[_i]) <= ENA_MAX_TX_DESCS_PER_PKT))     \
    })
#endif


/* ---------------- WF_MBUF: DPDK's mbuf contract -------------------- */
/* Environment stubs.  Left bodyless these are reported as "undefined function
 * should be unreachable" and, worse, the resulting path cut leaves the
 * loop-contract obligations UNDECIDED (reported UNKNOWN) rather than failing
 * loudly -- the same path-cutting effect as DFCC issue A.  So they are real
 * stubs, not abstractions. */
int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
	(void)level; (void)logtype; (void)format;
	return 0;
}


#if DISTINCT_MBUFS
static struct rte_mbuf *make_wf_mbuf_b(unsigned b, uint16_t nsegs)
{
  uint32_t total = 0;
  for(uint16_t i = 0; i < MAX_SEGS; i++)
  {
    struct rte_mbuf *m = &pkt_segs[b][i];
    m->buf_addr = pkt_bufs[b][i];
    m->buf_len = SEG_BUF_LEN;
    m->data_off = nd_u16();
    m->data_len = nd_u16();
    __CPROVER_assume(m->data_off <= SEG_BUF_LEN);
    __CPROVER_assume(m->data_len <= SEG_BUF_LEN - m->data_off);
    m->buf_iova = 0x1000 * (b * MAX_SEGS + i + 1);
    m->ol_flags = 0;
    m->next = (i + 1 < nsegs) ? &pkt_segs[b][i + 1] : NULL;
    if(i < nsegs)
      total += m->data_len;
  }
  pkt_segs[b][0].nb_segs = nsegs;
  pkt_segs[b][0].pkt_len = total;
  return &pkt_segs[b][0];
}
#endif

static struct rte_mbuf *make_wf_mbuf(uint16_t nsegs)
{
	uint32_t total = 0;
	/* NOTE: __CPROVER_havoc_object() havocs the WHOLE enclosing object, so
	 * it must run once before the links are written -- havocking inside the
	 * loop wipes the ->next set on the previous iteration. */
	__CPROVER_havoc_object(segs);
	for (uint16_t i = 0; i < MAX_SEGS; i++) {
		segs[i].buf_addr = seg_bufs[i];
		segs[i].buf_len = SEG_BUF_LEN;
		segs[i].data_off = nd_u16();
		segs[i].data_len = nd_u16();
		/* each segment's data lies inside its own buffer */
		__CPROVER_assume(segs[i].data_off <= SEG_BUF_LEN);
		__CPROVER_assume(segs[i].data_len <= SEG_BUF_LEN - segs[i].data_off);
		segs[i].buf_iova = 0x1000 * (i + 1);
		segs[i].ol_flags = 0;              /* no offloads requested */
		segs[i].next = (i + 1 < nsegs) ? &segs[i + 1] : NULL;
		if (i < nsegs)
			total += segs[i].data_len;
	}
	segs[0].nb_segs = nsegs;               /* chain length, per the contract */
	segs[0].pkt_len = total;               /* sum of the segment lengths */
	return &segs[0];
}

/* INV_DESC_SUM over the concrete ring: the descriptors outstanding on the SQ
 * are exactly those of the packets in flight.  The cleanup step assumes it,
 * so for the induction to compose every operation must preserve it.  It is a
 * sum, so it cannot be stated at a probe index. */
#if RELEASE_AFTER_XMIT
/*
 * The reset path's release, checked against ownership.  rte_eth_tx_burst
 * returns the number of packets the driver took; the rest still belong to the
 * caller.  ena_tx_queue_release_bufs (reached via ena_dev_reset ->
 * eth_ena_dev_uninit -> ena_close -> ena_stop -> ena_queue_stop) frees every
 * slot whose mbuf is non-NULL, so it must never free a packet that was
 * reported unsent.  The runner redirects calls to rte_pktmbuf_free here with
 * --replace-calls.
 */
static struct rte_mbuf *g_unsent;
void model_pktmbuf_free(struct rte_mbuf *m)
{
  __CPROVER_assert(
    m == NULL || m != g_unsent,
    "RELEASE_SAFE: queue release never frees a packet reported as unsent");
}
#endif

static bool INV_DESC_SUM_RING(const struct ena_ring *r)
{
  uint16_t sum = 0;
  for(unsigned k = 0; k < RING_SIZE; k++)
    if(r->tx_buffer_info[k].mbuf != NULL)
      sum = (uint16_t)(sum + TX_DESCS(r->tx_buffer_info[k]));
  return (uint16_t)(r->ena_com_io_sq->tail - r->ena_com_io_sq->next_to_comp) ==
         sum;
}

int main(void)
{
	struct ena_ring ring;
	struct ena_adapter adapter;
	struct ena_com_io_sq io_sq;
	struct ena_com_io_cq io_cq;
	static volatile uint32_t doorbell;
	static struct ena_tx_buffer tx_buffer_info[RING_SIZE];
	static uint16_t empty_tx_reqs[RING_SIZE];
	static uint8_t push_buf[128];
	struct rte_mbuf **tx_pkts;
	uint16_t nb_pkts, nsegs, sent, i;

	/* ---- arbitrary INV-state (the induction hypothesis) ---- */
	__CPROVER_havoc_object(&ring);
	__CPROVER_havoc_object(&adapter);
	__CPROVER_havoc_object(&io_sq);
	__CPROVER_havoc_object(&io_cq);
	__CPROVER_havoc_object(tx_buffer_info);
	/* The free list must be arbitrary too.  It was a zero-initialised static
	 * that nothing havocked until the composition check found it: every
	 * entry was 0, so every proof of this step only ever exercised slot 0,
	 * and a free list with distinct entries -- which INV_FREE_LIST requires
	 * for any ring with more than one free slot -- was not even satisfiable.
	 * The constraints on it come from INV_SLOTS_ASSUME and INV_FREE_LIST. */
	__CPROVER_havoc_object(empty_tx_reqs);
	/* struct ena_adapter embeds ena_com_dev by value (ena_ethdev.h:292) */
	ring.adapter = &adapter;
	ring.ena_com_io_sq = &io_sq;
	ring.ena_com_io_cq = &io_cq;
	ring.tx_buffer_info = tx_buffer_info;
	ring.empty_tx_reqs = empty_tx_reqs;
	ring.push_buf_intermediate_buf = push_buf;
	ring.ring_size = RING_SIZE;
	ring.size_mask = RING_SIZE - 1;
	ring.sgl_size = ENA_PKT_MAX_BUFS;
	ring.tx_max_header_size = 96;
	ring.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;  /* LLQ: exercises push path */
	/* Split the xmit obligation: this harness covers the path that does
	 * NOT call ena_tx_cleanup (tx_free_thresh == 0 makes the
	 * "available_desc < tx_free_thresh" branch unreachable).  Cleanup is a
	 * separate obligation with its own harness, which is the point of
	 * modular decomposition -- see stage 4 of the design doc. */
	ring.tx_free_thresh = 0;
	ring.next_to_use = nd_u16() & ring.size_mask;
	ring.next_to_clean = nd_u16() & ring.size_mask;
	ring.pkts_without_db = nd_u8() & 1;
	ring.offloads = 0;
	io_sq.db_addr = (uint32_t *)&doorbell;
	io_sq.q_depth = RING_SIZE;
	io_sq.tail = nd_u16();
	io_sq.next_to_comp = nd_u16();
	io_sq.mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
	io_sq.llq_info.descs_per_entry = nd_u16();
	io_sq.llq_info.descs_num_before_header = nd_u16();
	io_sq.llq_info.disable_meta_caching = false;
	__CPROVER_assume(INV_COM(&io_sq));
  adapter.state = ENA_ADAPTER_STATE_RUNNING;
  g_p1 = nd_u16();
	g_p2 = nd_u16();
	__CPROVER_assume(INV_STARTED(&ring));
  INV_SLOTS_ASSUME(empty_tx_reqs, tx_buffer_info);
  __CPROVER_assume(OWN_UNIQUE(&ring, g_p1, g_p2));
#if CHECK_DESC_SUM
#ifndef NO_ASSUME_DS
  __CPROVER_assume(INV_DESC_SUM_RING(&ring));
#endif
#ifndef NO_ASSUME_FL
  __CPROVER_assume(INV_FREE_LIST(&ring, RING_SIZE));
#endif
#endif

	/* ---- caller contract: tx_pkts holds exactly nb_pkts mbufs ---- */
	nb_pkts = nd_u16();
	__CPROVER_assume(nb_pkts >= 1 && nb_pkts <= MAX_BURST);
	/* eth_ena_xmit_pkts prefetches tx_pkts[(sent_idx + 4) & size_mask],
	 * masking with the RING mask rather than nb_pkts, so it reads up to
	 * ring_size entries whatever the burst length.  That is an implicit
	 * requirement on the caller's array (finding g1); allocate to it so
	 * the read is in bounds, and report the requirement separately. */
	tx_pkts = malloc(RING_SIZE * sizeof(*tx_pkts));
	__CPROVER_assume(tx_pkts != NULL);

	nsegs = nd_u16();
	__CPROVER_assume(nsegs >= 1 && nsegs <= MAX_SEGS);
#if SEGS_ASSUMED
	/* the app honoured the advertised tx_max_num_segs */
	__CPROVER_assume(nsegs < ring.sgl_size);
#endif
	for (i = 0; i < MAX_BURST; i++)
		if (i < nb_pkts)
#if DISTINCT_MBUFS
			tx_pkts[i] = make_wf_mbuf_b(i, nsegs);
#else
			tx_pkts[i] = make_wf_mbuf(nsegs);
#endif

	/*
	 * Ownership disjointness across the API boundary.  This is part of the
	 * rte_eth_tx_burst contract and the proof needs it stated: once the
	 * driver accepts an mbuf it owns it, and the application must not hand
	 * the same mbuf back while it is still in flight.  Without this clause
	 * OWN_UNIQUE is genuinely violable -- a slot already holding tx_pkts[i]
	 * plus xmit recording it again gives two owners, which on cleanup is a
	 * double free.  So the clause is not a convenience; it is the obligation
	 * the application carries, and it is worth telling the driver owners
	 * that nothing in the driver detects a violation of it.
	 */
	for (i = 0; i < MAX_BURST; i++)
		if (i < nb_pkts) {
			if (g_p1 < RING_SIZE)
				__CPROVER_assume(tx_buffer_info[g_p1].mbuf != tx_pkts[i]);
			if (g_p2 < RING_SIZE)
				__CPROVER_assume(tx_buffer_info[g_p2].mbuf != tx_pkts[i]);
		}

	/* the slot xmit will take for the first packet, captured before the
	 * call so the ownership transfer can be stated precisely */
	uint16_t ntu0 = ring.next_to_use;
	uint16_t req0 = empty_tx_reqs[ntu0];
#if DISTINCT_MBUFS
	uint16_t reqs[MAX_BURST];
	for (i = 0; i < MAX_BURST; i++)
		reqs[i] = empty_tx_reqs[(ntu0 + i) & (RING_SIZE - 1)];
#endif

	/* the packet in flight at an arbitrary slot, before the call */
	struct rte_mbuf *const held_p1 =
		g_p1 < RING_SIZE ? tx_buffer_info[g_p1].mbuf : NULL;

	/* ---- one arbitrary operation ---- */
	sent = eth_ena_xmit_pkts(&ring, tx_pkts, nb_pkts);

	/* xmit takes only free slots: a packet already in flight is still in
	 * its slot afterwards (otherwise it is overwritten and leaked, and its
	 * completion later releases the wrong packet) */
	if (held_p1 != NULL)
		__CPROVER_assert(tx_buffer_info[g_p1].mbuf == held_p1,
				 "OWN_NO_LOSS: a packet in flight keeps its slot");

	/* ---- obligations ---- */
#if VACUITY_CHECK
  /* expected to FAIL; a SUCCESS here means the harness proves nothing
	 * about the accept path */
  __CPROVER_assert(sent == 0, "vacuity witness: a packet can be accepted");
#endif
  __CPROVER_assert(sent <= nb_pkts, "API: sent <= nb_pkts");
	__CPROVER_assert(INV_STARTED(&ring), "INV_STARTED preserved by eth_ena_xmit_pkts");
  __CPROVER_assert(
    INV_SLOT(&ring, g_p1), "INV_SLOT preserved for an arbitrary ring slot");
  /* INV_wakeup, checked here with the REAL ena_xmit_mbuf body, so the
	 * mid-loop LLQ doorbell write is in scope too */
	__CPROVER_assert(!ring.pkts_without_db,
			 "INV_wakeup: no packet left pushed without a doorbell");

	/* ownership map, part 1: uniqueness is preserved */
#if WITNESS_PREPARE_FAIL
  /* must FAIL: otherwise the prepare_tx error path (F12) is unreachable in
   * this harness and every obligation is silent about it */
  __CPROVER_assert(!g_prepare_tx_failed, "witness: prepare_tx can fail");
#endif
#if CHECK_DESC_SUM
  __CPROVER_assert(
    INV_DESC_SUM_RING(&ring), "INV_DESC_SUM preserved by eth_ena_xmit_pkts");
  __CPROVER_assert(
    INV_FREE_LIST(&ring, RING_SIZE),
    "INV_FREE_LIST preserved by eth_ena_xmit_pkts");
#endif
#if RELEASE_AFTER_XMIT
  /* then the queue is stopped, as the reset requested by a failed
   * ena_com_prepare_tx will do */
  g_unsent = (sent < nb_pkts) ? tx_pkts[sent] : NULL;
  ena_tx_queue_release_bufs(&ring);
#endif
	__CPROVER_assert(OWN_UNIQUE(&ring, g_p1, g_p2),
			 "OWN_UNIQUE: no mbuf owned by two tx_buffer_info slots");
	/* ownership map, part 2: an accepted packet is transferred into the
	 * slot the free list named, so it is neither lost nor duplicated */
#if DISTINCT_MBUFS
	{
		bool transferred = true, unsent_free = true;
		for (i = 0; i < MAX_BURST; i++) {
			if (i < sent && ring.tx_buffer_info[reqs[i]].mbuf != tx_pkts[i])
				transferred = false;
			if (i >= sent && i < nb_pkts)
				for (unsigned k = 0; k < RING_SIZE; k++)
					if (ring.tx_buffer_info[k].mbuf == tx_pkts[i])
						unsent_free = false;
		}
		__CPROVER_assert(transferred,
				 "OWN_TRANSFER: every accepted mbuf recorded in its req_id slot");
		/* F12 stated directly: the caller still owns a packet reported as
		 * unsent, so no slot may refer to it */
		__CPROVER_assert(unsent_free,
				 "OWN_UNSENT: no slot holds a packet reported as unsent");
#if VACUITY_CHECK
		__CPROVER_assert(sent < MAX_BURST,
				 "vacuity witness: a full burst can be accepted");
#endif
	}
#else
	if (sent >= 1)
		__CPROVER_assert(ring.tx_buffer_info[req0].mbuf == tx_pkts[0],
				 "OWN_TRANSFER: accepted mbuf recorded in its req_id slot");
#endif
	return 0;
}
