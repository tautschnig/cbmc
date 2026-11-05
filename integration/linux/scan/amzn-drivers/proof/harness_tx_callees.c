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
int ena_com_prepare_tx(
  struct ena_com_io_sq *io_sq,
  struct ena_com_tx_ctx *ena_tx_ctx,
  int *nb_hw_desc)
{
  int rc;
  uint16_t descs;

  rc = (int)nd_u16();
  if (rc != 0)
    return rc;                      /* the device/queue rejected the request */

  descs = nd_u16();
  __CPROVER_assume(descs >= 1 && descs <= (uint16_t)ena_tx_ctx->num_bufs + 2);
  __CPROVER_assume(
    (uint16_t)(io_sq->tail + descs - io_sq->next_to_comp) <= io_sq->q_depth - 1);
  *nb_hw_desc = (int)descs;
  io_sq->tail = (uint16_t)(io_sq->tail + descs);
  return 0;
}

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }

/* mbuf pool, defined ahead of INV_RING because the invariant refers to it */
static struct rte_mbuf segs[MAX_SEGS];
static uint8_t seg_bufs[MAX_SEGS][SEG_BUF_LEN];

/* ---------------- INV: what a valid ENA TX queue is ---------------- */
/*
 * INV_COM: the ena_com-layer invariant.  Discovered by proof failure, not by
 * reading the code: ena_com_sq_have_enough_space() and
 * ena_com_is_doorbell_needed() divide by llq_info.descs_per_entry, so a valid
 * LLQ-mode SQ must have descs_per_entry >= 1.  ena_com_config_dev_mode()
 * establishes this (descs_per_entry = desc_list_entry_size / 16 with
 * desc_list_entry_size in {128,192,256}); proving THAT is part of the base
 * case.  Worth noting the fragility: a driver that ever configured
 * llq_ring_entry_size_value < 16 would divide by zero here, and only the
 * `& 0x7` multiple-of-8 check stands in the way.
 */
static bool INV_COM(const struct ena_com_io_sq *sq)
{
	/* SQ accounting: no more descriptors in flight than the ring holds.
	 * Also discovered by proof failure -- ena_com_free_q_entries() returns
	 * `q_depth - (u16)(tail - next_to_comp) - 1` as an int, so without this
	 * clause it can go negative and make the cleanup branch of
	 * eth_ena_xmit_pkts reachable from a state the driver never reaches. */
	return (uint16_t)(sq->tail - sq->next_to_comp) <= sq->q_depth - 1 &&
	       sq->llq_info.descs_per_entry >= 1 &&
	       sq->llq_info.descs_per_entry <= 16 &&
	       sq->llq_info.descs_num_before_header <= 16 &&
	       sq->llq_info.disable_meta_caching == false;
}

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
         (tbi[p].mbuf == NULL || tbi[p].mbuf == &segs[0]) &&
         tbi[p].num_of_bufs <= ENA_PKT_MAX_BUFS;
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
        __CPROVER_assume(                                                      \
          (tbi)[_i].mbuf == 0 || (tbi)[_i].mbuf == &segs[0]);                  \
        __CPROVER_assume((tbi)[_i].num_of_bufs <= ENA_PKT_MAX_BUFS);            \
      }                                                                        \
    } while(0)
#else
#  define INV_SLOTS_ASSUME(etr, tbi)                                           \
    __CPROVER_assume(__CPROVER_forall {                                        \
      unsigned _i;                                                             \
      _i < RING_SIZE ==> ((etr)[_i] < RING_SIZE &&                             \
                          ((tbi)[_i].mbuf == 0 ||                              \
                           (tbi)[_i].mbuf == &segs[0]) &&                      \
                          (tbi)[_i].num_of_bufs <= ENA_PKT_MAX_BUFS)           \
    })
#endif

static bool INV_RING(const struct ena_ring *r)
{
	bool ok = INV_COM(r->ena_com_io_sq) &&
		  r->adapter != NULL &&
		  r->ena_com_io_sq != NULL && r->ena_com_io_cq != NULL &&
		  r->ena_com_io_sq->db_addr != NULL &&
		  r->tx_buffer_info != NULL && r->empty_tx_reqs != NULL &&
		  r->push_buf_intermediate_buf != NULL &&
		  /* geometry, established by ena_tx_queue_setup */
		  r->size_mask == r->ring_size - 1 &&
		  rte_is_power_of_2(r->ring_size) &&
		  r->sgl_size <= ENA_PKT_MAX_BUFS &&
		  r->next_to_use <= r->size_mask &&
		  r->next_to_clean <= r->size_mask &&
		  /* SQ accounting */
		  r->ena_com_io_sq->q_depth == r->ring_size;
  /* The per-slot clauses moved out of here.  They used to be two loops
	 * over RING_SIZE, which made every proof cost grow with the ring size
	 * for no benefit: the operation touches at most MAX_BURST slots, and the
	 * *preserved* property is about an arbitrary slot.  They are now a
	 * quantified assumption (INV_SLOTS_ASSUME) plus an assertion at one
	 * nondeterministic index (INV_SLOT), which is equivalent and costs O(1)
	 * in the ring size. */
  return ok;
}

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
	__CPROVER_assume(INV_RING(&ring));
  INV_SLOTS_ASSUME(empty_tx_reqs, tx_buffer_info);
  __CPROVER_assume(OWN_UNIQUE(&ring, g_p1, g_p2));

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
			tx_pkts[i] = make_wf_mbuf(nsegs);

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

	/* ---- one arbitrary operation ---- */
	sent = eth_ena_xmit_pkts(&ring, tx_pkts, nb_pkts);

	/* ---- obligations ---- */
#if VACUITY_CHECK
  /* expected to FAIL; a SUCCESS here means the harness proves nothing
	 * about the accept path */
  __CPROVER_assert(sent == 0, "vacuity witness: a packet can be accepted");
#endif
  __CPROVER_assert(sent <= nb_pkts, "API: sent <= nb_pkts");
	__CPROVER_assert(INV_RING(&ring), "INV preserved by eth_ena_xmit_pkts");
  __CPROVER_assert(
    INV_SLOT(&ring, g_p1), "INV_SLOT preserved for an arbitrary ring slot");
  /* INV_wakeup, checked here with the REAL ena_xmit_mbuf body, so the
	 * mid-loop LLQ doorbell write is in scope too */
	__CPROVER_assert(!ring.pkts_without_db,
			 "INV_wakeup: no packet left pushed without a doorbell");

	/* ownership map, part 1: uniqueness is preserved */
	__CPROVER_assert(OWN_UNIQUE(&ring, g_p1, g_p2),
			 "OWN_UNIQUE: no mbuf owned by two tx_buffer_info slots");
	/* ownership map, part 2: an accepted packet is transferred into the
	 * slot the free list named, so it is neither lost nor duplicated */
	if (sent >= 1)
		__CPROVER_assert(ring.tx_buffer_info[req0].mbuf == tx_pkts[0],
				 "OWN_TRANSFER: accepted mbuf recorded in its req_id slot");
	return 0;
}
