/*
 * Stage 2: remove the burst bound.  Same induction step as
 * harness_tx_allseq.c, but nb_pkts is UNCONSTRAINED (any uint16_t) and the
 * TX loop is discharged by a loop invariant supplied out-of-band in
 * tx_loop.json, so no driver source is annotated and nothing is unrolled.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"

#ifndef RING_SIZE
#define RING_SIZE 128
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }

static bool INV(const struct ena_ring *r)
{
	return r->ena_com_io_sq != NULL && r->ena_com_io_cq != NULL &&
	       r->ena_com_io_sq->db_addr != NULL &&
	       r->size_mask == r->ring_size - 1 &&
	       r->ring_size >= ENA_MIN_RING_DESC &&
	       rte_is_power_of_2(r->ring_size) &&
	       r->tx_free_thresh <= r->ring_size &&
	       r->ena_com_io_sq->q_depth == r->ring_size &&
	       (uint16_t)(r->ena_com_io_sq->tail -
			  r->ena_com_io_sq->next_to_comp) <=
		       r->ena_com_io_sq->q_depth - 1;
}

int main(void)
{
	struct ena_ring ring;
	struct ena_adapter adapter;
	struct ena_com_io_sq io_sq;
	struct ena_com_io_cq io_cq;
	static volatile uint32_t doorbell;
	struct rte_mbuf **tx_pkts;
	uint16_t nb_pkts, sent;

	__CPROVER_havoc_object(&ring);
	__CPROVER_havoc_object(&adapter);
	__CPROVER_havoc_object(&io_sq);
	__CPROVER_havoc_object(&io_cq);
	ring.adapter = &adapter;
	ring.ena_com_io_sq = &io_sq;
	ring.ena_com_io_cq = &io_cq;
	ring.ring_size = RING_SIZE;
	ring.size_mask = RING_SIZE - 1;
	ring.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;
	ring.tx_free_thresh = 0;        /* cleanup is a separate obligation */
	ring.next_to_use = nd_u16() & ring.size_mask;
	ring.next_to_clean = nd_u16() & ring.size_mask;
	ring.pkts_without_db = nd_u8() & 1;
	io_sq.db_addr = (uint32_t *)&doorbell;
	io_sq.q_depth = RING_SIZE;
	io_sq.tail = nd_u16();
	io_sq.next_to_comp = nd_u16();
	adapter.state = ENA_ADAPTER_STATE_RUNNING;
	__CPROVER_assume(INV(&ring));

	/* ---- NO bound on the burst length ---- */
	nb_pkts = nd_u16();
	__CPROVER_assume(nb_pkts >= 1);
	tx_pkts = malloc((size_t)nb_pkts * sizeof(*tx_pkts));
	__CPROVER_assume(tx_pkts != NULL);

	sent = eth_ena_xmit_pkts(&ring, tx_pkts, nb_pkts);

	__CPROVER_assert(sent <= nb_pkts, "API: sent <= nb_pkts");
	__CPROVER_assert(INV(&ring), "INV preserved");

  /*
	 * INV_wakeup, the doorbell handshake: a lost wakeup is not a statement
	 * about the future, it is a reachable state in which work is pending
	 * and nobody is going to look at it.  Here the pending-work flag is
	 * pkts_without_db, set by the TX loop for every accepted packet and
	 * cleared only by a doorbell write, so the obligation on return is that
	 * no packet is left pushed-but-not-rung.  Pure safety, no temporal
	 * operator, and it costs one assertion.
	 */
  __CPROVER_assert(
    !ring.pkts_without_db,
    "INV_wakeup: no packet left pushed without a doorbell");
  /* referenced so goto-instrument --add-library pulls in the model that
	 * DFCC's write-set bookkeeping needs */
	free(tx_pkts);
	return 0;
}
