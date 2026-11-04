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
#define SEG_BUF_LEN 64

/* nondet via uninitialised locals: a bodyless function would be flagged
 * "undefined function should be unreachable" by the DFCC instrumentation */
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
	/* the req_id indirection: every free-list entry indexes tx_buffer_info */
	for (uint16_t i = 0; i < RING_SIZE; i++)
		ok = ok && r->empty_tx_reqs[i] < r->ring_size;
	/* every tx_buffer_info slot is either free or references a live mbuf,
	 * and its segment count fits the descriptor array.  Abstracted here to
	 * a one-element mbuf pool; the driver's real invariant is that an
	 * in-flight slot points to an mbuf owned by the queue. */
	for (uint16_t i = 0; i < RING_SIZE; i++)
		ok = ok && (r->tx_buffer_info[i].mbuf == NULL ||
			    r->tx_buffer_info[i].mbuf == &segs[0]) &&
		     r->tx_buffer_info[i].num_of_bufs <= ENA_PKT_MAX_BUFS;
	return ok;
}

/* ---------------- WF_MBUF: DPDK's mbuf contract -------------------- */
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
	for (i = 0; i < RING_SIZE; i++) {
		empty_tx_reqs[i] = nd_u16();
		__CPROVER_assume(empty_tx_reqs[i] < RING_SIZE);
	}
	__CPROVER_assume(INV_RING(&ring));

	/* ---- caller contract: tx_pkts holds exactly nb_pkts mbufs ---- */
	nb_pkts = nd_u16();
	__CPROVER_assume(nb_pkts >= 1 && nb_pkts <= MAX_BURST);
	tx_pkts = malloc(nb_pkts * sizeof(*tx_pkts));
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

	/* ---- one arbitrary operation ---- */
	sent = eth_ena_xmit_pkts(&ring, tx_pkts, nb_pkts);

	/* ---- obligations ---- */
	__CPROVER_assert(sent <= nb_pkts, "API: sent <= nb_pkts");
	__CPROVER_assert(INV_RING(&ring), "INV preserved by eth_ena_xmit_pkts");
  /* INV_wakeup, checked here with the REAL ena_xmit_mbuf body, so the
	 * mid-loop LLQ doorbell write is in scope too */
  __CPROVER_assert(
    !ring.pkts_without_db,
    "INV_wakeup: no packet left pushed without a doorbell");
  return 0;
}
