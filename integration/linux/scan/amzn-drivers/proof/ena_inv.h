/*
 * Shared invariant definitions for the ENA induction proof.
 *
 * These live in one place on purpose.  The induction only composes if the
 * state the step ASSUMES is exactly the state the base case ESTABLISHES, and
 * the first version of this proof got that wrong: harness_tx_callees.c
 * assumed the ena_com io_sq clauses while harness_setup_basecase.c proved
 * only the INV_CONFIGURED subset, so the two halves did not meet and there
 * was no induction.  Keeping one definition per predicate makes that class of
 * gap a compile-time impossibility rather than something to be noticed by
 * reading two files side by side.
 *
 * The predicate hierarchy mirrors the driver's own lifecycle:
 *
 *   INV_DEV_LLQ       device level, established by ena_com_config_dev_mode()
 *                     during device init.  Queue start neither establishes
 *                     nor disturbs it, so queue start assumes it.
 *   INV_CONFIGURED    established by ena_tx_queue_setup()
 *   INV_STARTED       = INV_CONFIGURED + INV_DEV_LLQ + the io_sq/io_cq
 *                     handles, established by ena_queue_start().  This is
 *                     the state every TX operation assumes and preserves.
 */

#ifndef ENA_INV_H
#define ENA_INV_H

/*
 * INV_DEV_LLQ: the LLQ geometry.  Discovered by proof failure, not by reading
 * the code: ena_com_sq_have_enough_space() and ena_com_is_doorbell_needed()
 * divide by llq_info.descs_per_entry, so a valid LLQ-mode SQ must have
 * descs_per_entry >= 1.  ena_com_config_dev_mode() establishes this
 * (descs_per_entry = desc_list_entry_size / 16 with desc_list_entry_size in
 * {128,192,256}).  Worth noting the fragility: a driver that ever configured
 * llq_ring_entry_size_value < 16 would divide by zero here, and only the
 * `& 0x7` multiple-of-8 check stands in the way.
 */
static bool INV_DEV_LLQ(const struct ena_com_io_sq *sq)
{
  return sq->llq_info.descs_per_entry >= 1 &&
         sq->llq_info.descs_per_entry <= 16 &&
         sq->llq_info.descs_num_before_header <= 16 &&
         sq->llq_info.disable_meta_caching == false;
}

/*
 * INV_SQ_ACCOUNTING: no more descriptors in flight than the ring holds.  Also
 * discovered by proof failure -- ena_com_free_q_entries() returns
 * `q_depth - (u16)(tail - next_to_comp) - 1` as an int, so without this
 * clause it can go negative and make the cleanup branch of eth_ena_xmit_pkts
 * reachable from a state the driver never reaches.
 */
static bool INV_SQ_ACCOUNTING(const struct ena_com_io_sq *sq)
{
  return (uint16_t)(sq->tail - sq->next_to_comp) <= sq->q_depth - 1;
}

/* The ena_com-layer invariant the TX path depends on. */
static bool INV_COM(const struct ena_com_io_sq *sq)
{
  return INV_SQ_ACCOUNTING(sq) && INV_DEV_LLQ(sq);
}

/*
 * Upper bound on the hardware descriptors one packet occupies:
 * ena_com_prepare_tx emits at most one per buffer plus a meta descriptor and a
 * header descriptor (the stage-1 callee contract for ena_com_prepare_tx states
 * exactly this).  Every harness must use the same bound: with no bound the
 * uint16_t sum in INV_DESC_SUM admits states that satisfy it only by
 * wrap-around, and with different bounds one step can produce a state the
 * next step does not assume -- both happened.
 */
#define ENA_MAX_TX_DESCS_PER_PKT (ENA_PKT_MAX_BUFS + 2)

/* Ring geometry, established by ena_tx_queue_setup and never changed after. */
static bool INV_GEOMETRY(const struct ena_ring *r)
{
  return r->size_mask == r->ring_size - 1 &&
         rte_is_power_of_2(r->ring_size) &&
         r->sgl_size <= ENA_PKT_MAX_BUFS;
}

/*
 * INV_CONFIGURED: the post-state of ena_tx_queue_setup.  Deliberately does
 * NOT mention ena_com_io_sq / ena_com_io_cq: at this point the queue has
 * memory and geometry but no device queue, and dereferencing those handles
 * would be wrong.  They arrive with INV_STARTED.
 */
static bool INV_CONFIGURED(const struct ena_ring *r)
{
  return r->configured == 1 && r->tx_buffer_info != NULL &&
         r->empty_tx_reqs != NULL && r->push_buf_intermediate_buf != NULL &&
         INV_GEOMETRY(r) && r->next_to_use == 0 && r->next_to_clean == 0 &&
         r->pkts_without_db == false;
}

/*
 * INV_STARTED: the state a started TX queue is in, which is exactly what
 * every TX operation assumes on entry and must re-establish on exit.  This is
 * the predicate the induction turns on, so it is the one definition that
 * must not be duplicated anywhere.
 */
static bool INV_STARTED(const struct ena_ring *r)
{
  return INV_COM(r->ena_com_io_sq) && r->adapter != NULL &&
         r->ena_com_io_sq != NULL && r->ena_com_io_cq != NULL &&
         r->ena_com_io_sq->db_addr != NULL && r->tx_buffer_info != NULL &&
         r->empty_tx_reqs != NULL && r->push_buf_intermediate_buf != NULL &&
         INV_GEOMETRY(r) && r->next_to_use <= r->size_mask &&
         r->next_to_clean <= r->size_mask &&
         r->ena_com_io_sq->q_depth == r->ring_size;
}


/*
 * INV_FREE_LIST: the free list names exactly the free slots.
 *
 * empty_tx_reqs is a ring of slot indices.  xmit takes the slot named at
 * next_to_use and advances it; cleanup writes the completed slot back at
 * next_to_clean and advances that.  With inflight = (next_to_use -
 * next_to_clean) & size_mask packets outstanding, the window of
 * ring_size - inflight positions starting at next_to_use is the free list.
 * The invariant: those positions name pairwise distinct, free slots, and the
 * number of occupied slots is inflight, so they are exactly the free slots.
 * Each occupied slot also accounts for at least one descriptor, which is what
 * keeps inflight below ring_size.
 *
 * The driver relies on this -- ena_xmit_mbuf only RTE_ASSERTs that the slot it
 * takes is free, which is compiled out in release builds -- and neither step
 * could be proven to compose without it: the xmit loop contract otherwise
 * admits a state where the slot at next_to_use already holds a packet.
 *
 * Stated over the concrete ring size n, because it quantifies over positions
 * and a probe index cannot express "pairwise distinct".
 */
static bool INV_FREE_LIST(const struct ena_ring *r, unsigned n)
{
  const uint16_t inflight =
    (uint16_t)((r->next_to_use - r->next_to_clean) & r->size_mask);
  const unsigned window = n - inflight;
  unsigned occupied = 0;

  for(unsigned k = 0; k < n; k++)
  {
    if(r->tx_buffer_info[k].mbuf != NULL)
    {
      occupied++;
      if(
        r->tx_buffer_info[k].tx_descs < 1 ||
        r->tx_buffer_info[k].tx_descs > ENA_MAX_TX_DESCS_PER_PKT)
        return false;
    }
  }
  if(occupied != inflight)
    return false;

  for(unsigned j = 0; j < n; j++)
  {
    if(j >= window)
      break;
    const uint16_t id = r->empty_tx_reqs[(r->next_to_use + j) & r->size_mask];
    if(id >= n || r->tx_buffer_info[id].mbuf != NULL)
      return false;
    for(unsigned j2 = j + 1; j2 < n; j2++)
    {
      if(j2 >= window)
        break;
      if(r->empty_tx_reqs[(r->next_to_use + j2) & r->size_mask] == id)
        return false;
    }
  }
  return true;
}

#endif /* ENA_INV_H */
