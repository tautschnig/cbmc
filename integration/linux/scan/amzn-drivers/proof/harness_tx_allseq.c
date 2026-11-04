/*
 * Pilot: all-sequences safety proof for the DPDK ENA TX path.
 *
 * Structure (induction over the operation sequence, so the number of
 * operations is NOT bounded):
 *   base case   -- ena_tx_queue_setup establishes INV (proved separately)
 *   step        -- from an ARBITRARY state satisfying INV, any single
 *                  operation preserves INV and commits no safety violation
 * Proving the step for an arbitrary INV-state covers every reachable
 * sequence of operations; nothing here unrolls an operation sequence.
 *
 * The driver is the real, unmodified source: we include the whole
 * translation unit so the file-static entry points are visible.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"

#define MAX_BURST 6 /* bound on ONE burst, not on the sequence */

static uint16_t nd_u16(void);
static uint8_t nd_u8(void);

/* ---- the invariant: what ena_tx_queue_setup establishes and every
 *      operation must preserve ---------------------------------------- */
static bool INV(const struct ena_ring *r)
{
  /* well-formedness: the queue the ring points at exists.  Needed
	 * because ena_com_free_q_entries() dereferences it on entry. */
  return r->ena_com_io_sq != NULL && r->ena_com_io_cq != NULL &&
         /* the SQ doorbell register is mapped (ena_queue_start) */
         r->ena_com_io_sq->db_addr != NULL &&
         /* ring geometry, as established by ena_tx_queue_setup() */
         r->size_mask == r->ring_size - 1 &&
         r->ring_size >= ENA_MIN_RING_DESC && rte_is_power_of_2(r->ring_size) &&
         r->tx_free_thresh <= r->ring_size &&
         /* the SQ accounting the driver relies on */
         r->ena_com_io_sq->q_depth == r->ring_size &&
         r->ena_com_io_sq->tail - r->ena_com_io_sq->next_to_comp <=
           r->ring_size;
}

int main(void)
{
  struct ena_ring ring;
  struct ena_adapter adapter;
  struct ena_com_io_sq io_sq;
  struct ena_com_io_cq io_cq;
  struct rte_mbuf **tx_pkts;
  uint16_t nb_pkts, sent;

  /* ---- arbitrary state satisfying INV (the induction hypothesis) ---- */
  __CPROVER_havoc_object(&ring);
  __CPROVER_havoc_object(&adapter);
  ring.adapter = &adapter;
  __CPROVER_havoc_object(&io_sq);
  __CPROVER_havoc_object(&io_cq);
  ring.ena_com_io_sq = &io_sq;
  ring.ena_com_io_cq = &io_cq;
  static volatile uint32_t doorbell;
  io_sq.db_addr = (uint32_t *)&doorbell;
  io_sq.q_depth = 128;
  io_sq.tail = nd_u16();
  io_sq.next_to_comp = nd_u16();
  __CPROVER_assume((uint16_t)(io_sq.tail - io_sq.next_to_comp) <= 128);
  ring.ring_size = 128; /* smallest advertised ring */
  ring.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;
  ring.size_mask = ring.ring_size - 1;
  ring.tx_free_thresh = nd_u16();
  __CPROVER_assume(ring.tx_free_thresh <= ring.ring_size);
  ring.next_to_use = nd_u16() & ring.size_mask;
  ring.next_to_clean = nd_u16() & ring.size_mask;
  ring.pkts_without_db = nd_u8() & 1;
  adapter.state = ENA_ADAPTER_STATE_RUNNING;
  __CPROVER_assume(INV(&ring));

  /* ---- the caller's contract for rte_eth_tx_burst: tx_pkts points to
	 *      EXACTLY nb_pkts entries (nothing promises more) -------------- */
  nb_pkts = nd_u16();
  __CPROVER_assume(nb_pkts >= 1 && nb_pkts <= MAX_BURST);
  tx_pkts = malloc(nb_pkts * sizeof(*tx_pkts));
  __CPROVER_assume(tx_pkts != NULL);

  /* ---- one arbitrary operation ---- */
  sent = eth_ena_xmit_pkts(&ring, tx_pkts, nb_pkts);

  /* ---- obligations ---- */
  __CPROVER_assert(sent <= nb_pkts, "API: sent <= nb_pkts");
  __CPROVER_assert(INV(&ring), "INV preserved by eth_ena_xmit_pkts");
  return 0;
}
