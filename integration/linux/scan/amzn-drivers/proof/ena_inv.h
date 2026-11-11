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

/*
 * The descriptor count a slot records.  ena_xmit_mbuf stores the int
 * nb_hw_desc = (int)new_tail - (int)old_tail into the unsigned tx_descs, so
 * when the u16 SQ tail wraps it holds (unsigned)(lines - 65536).  Every
 * consumer sums tx_descs and passes the sum through ena_com_comp_ack's u16,
 * so the driver is correct modulo 2^16; the invariants state it that way.
 * (harness_prepare_tx.c, PTX_NB.)
 */
#define TX_DESCS(b) ((uint16_t)(b).tx_descs)

/*
 * The lower bound on a slot's descriptor count.  ena_com_prepare_tx writes no
 * descriptor for an empty packet (header_len == 0, num_bufs == 0, no meta
 * descriptor) and still returns success (PTX_DESCS_MIN), so an in-flight
 * packet can hold 0 descriptors unless the application never sends one.
 * -DALLOW_EMPTY_IN_FLIGHT states the invariant without that obligation.
 */
#ifdef ALLOW_EMPTY_IN_FLIGHT
#  define TX_DESCS_MIN 0
#else
#  define TX_DESCS_MIN 1
#endif

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
 * INV_STARTABLE: what ena_queue_start needs of a TX ring, and what both ways
 * of reaching it must establish -- ena_tx_queue_setup (first start) and
 * ena_queue_stop's ena_tx_queue_release_bufs (restart after stop or reset).
 * Weaker than INV_CONFIGURED on purpose: a stopped ring keeps its old
 * next_to_use / next_to_clean / pkts_without_db and a stale free list, and
 * ena_queue_start rewrites the first two and ena_create_io_queue the free
 * list.  What it does not rewrite is the slots, so every slot must be free;
 * and a stale num_of_bufs must still be in range for INV_SLOT.
 */
static bool INV_STARTABLE(const struct ena_ring *r, unsigned n)
{
  if(!(r->configured == 1 && r->tx_buffer_info != NULL &&
       r->empty_tx_reqs != NULL && r->push_buf_intermediate_buf != NULL &&
       INV_GEOMETRY(r) && r->ring_size == n))
    return false;
  for(unsigned k = 0; k < n; k++)
    if(
      r->tx_buffer_info[k].mbuf != NULL ||
      r->tx_buffer_info[k].num_of_bufs > ENA_PKT_MAX_BUFS)
      return false;
  return true;
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
         r->ena_com_io_sq->q_depth == r->ring_size &&
         /* the xmit step covers LLQ mode only; pinning it here makes a base
          * case that starts the queue in another mode fail to compose */
         r->tx_mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV &&
         r->ena_com_io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV;
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
        TX_DESCS(r->tx_buffer_info[k]) < TX_DESCS_MIN ||
        TX_DESCS(r->tx_buffer_info[k]) > ENA_MAX_TX_DESCS_PER_PKT)
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

/*
 * INV_RX_STARTABLE: what ena_queue_start needs of an RX ring.  The RX
 * analogue of INV_STARTABLE, and for the same reason: ena_create_io_queue
 * rewrites empty_rx_reqs and the indices, but not the slots, and
 * ena_queue_start then calls ena_populate_rx_queue(ring_size - 1), which
 * would overwrite any slot that still held a buffer.  So every slot must be
 * free.  Both ena_rx_queue_setup (first start) and ena_queue_stop's
 * ena_rx_queue_release_bufs (restart after stop or reset) must establish it.
 */
static bool INV_RX_STARTABLE(const struct ena_ring *r, unsigned n)
{
  if(!(r->configured == 1 && r->rx_buffer_info != NULL &&
       r->empty_rx_reqs != NULL && r->rx_refill_buffer != NULL &&
       r->mb_pool != NULL && INV_GEOMETRY(r) && r->ring_size == n))
    return false;
  struct ena_rx_buffer *const rbi = r->rx_buffer_info;
  for(unsigned k = 0; k < n; k++)
    if(rbi[k].mbuf != NULL)
      return false;
  return true;
}

/*
 * INV_RX_STARTED: a started RX queue.  The RX analogue of INV_STARTED, over
 * the rx_buffer_info / empty_rx_reqs arrays.
 *
 *   - the handles and pool are non-NULL, geometry is consistent, LLQ is not
 *     used on RX (the SQ carries posted buffers, placement is HOST);
 *   - every posted buffer is owned by exactly one slot: the occupied slots
 *     are exactly those an in-flight (posted-not-completed) descriptor names.
 *     As with TX this is stated through the free list: the window of
 *     ring_size - inflight positions of empty_rx_reqs starting at
 *     next_to_use names pairwise-distinct free (mbuf == NULL) slots, so the
 *     occupied slots are exactly the inflight ones.
 *
 * inflight here is the number of buffers the device has not yet completed,
 * (next_to_use - next_to_clean) & size_mask, the same accounting as TX.
 */
static bool INV_RX_STARTED(const struct ena_ring *r, unsigned n)
{
  if(!(r->adapter != NULL && r->ena_com_io_sq != NULL &&
       r->ena_com_io_cq != NULL && r->rx_buffer_info != NULL &&
       r->empty_rx_reqs != NULL && r->rx_refill_buffer != NULL &&
       r->mb_pool != NULL && INV_GEOMETRY(r) && r->ring_size == n &&
       r->next_to_use <= r->size_mask && r->next_to_clean <= r->size_mask &&
       r->ena_com_io_sq->q_depth == r->ring_size &&
       /* the device has not completed more than was posted: used entries are
        * in range, so free entries are in 0..q_depth-1 */
       (uint16_t)(r->ena_com_io_sq->tail - r->ena_com_io_sq->next_to_comp) <=
         r->ena_com_io_sq->q_depth - 1 &&
       /* RX accounting, the analogue of INV_DESC_SUM: the ring's in-flight
        * count and the SQ's used entries are the same number.  Refill
        * advances both next_to_use and sq->tail; recv advances next_to_clean
        * and (through ena_com_rx_pkt) sq->next_to_comp.  Without this clause
        * the number of free slots and the number of free SQ entries are
        * unrelated, and refill called with the free SQ entries -- which is
        * what eth_ena_recv_pkts passes -- can overrun the free list. */
       ((uint16_t)((r->next_to_use - r->next_to_clean) & r->size_mask)) ==
         (uint16_t)(r->ena_com_io_sq->tail - r->ena_com_io_sq->next_to_comp)))
    return false;

  /* Copy the anonymous-union members into typed locals: accessing
   * r->rx_buffer_info / r->empty_rx_reqs directly builds a byte_extract over
   * the union whose object CBMC's pointer analysis cannot size, so the
   * bounds checks come back ERROR. */
  struct ena_rx_buffer *const rbi = r->rx_buffer_info;
  uint16_t *const err = r->empty_rx_reqs;
  const uint16_t inflight =
    (uint16_t)(((uint16_t)(r->next_to_use - r->next_to_clean)) & r->size_mask);
  const unsigned window = n - inflight;
  unsigned occupied = 0;

  for(unsigned k = 0; k < n; k++)
    if(rbi[k].mbuf != NULL)
      occupied++;
  if(occupied != inflight)
    return false;

  for(unsigned j = 0; j < n; j++)
  {
    if(j >= window)
      break;
    const uint16_t id = err[(r->next_to_use + j) & r->size_mask];
    if(id >= n || rbi[id].mbuf != NULL)
      return false;
    for(unsigned j2 = j + 1; j2 < n; j2++)
    {
      if(j2 >= window)
        break;
      if(err[(r->next_to_use + j2) & r->size_mask] == id)
        return false;
    }
  }
  return true;
}

/*
 * INV_FREE_LIST_FAST: the same predicate as INV_FREE_LIST, stated with two
 * bitmasks instead of a pairwise comparison of window positions.
 *
 * The pairwise form is O(n^2) and is evaluated twice per proof run (once
 * assumed, once asserted), which made it the dominant cost of the cleanup
 * step and is why ring 8 did not finish.  This form is O(n):
 *
 *   window_mask  the ids named by the free window, as a bit per slot
 *   free_mask    the slots that are actually free, as a bit per slot
 *
 * and the three clauses are: every window id is in range, the two masks are
 * equal, and the mask has exactly `window` bits set.  The last two give
 * distinctness without comparing pairs -- a repeated id sets one bit twice,
 * so the popcount would come out below `window`.
 *
 * harness_freelist_equiv.c proves the two forms accept exactly the same
 * states, so this one can be used wherever the other was.  The mask is 64
 * bits, so n <= 64; at ring 64 a 32-bit mask reported "shift distance too
 * large", which is how the limit was found rather than assumed.  Beyond 64 it
 * would need an array of words, or the pairwise form, which has no width
 * limit but is O(n^2).
 */
static bool INV_FREE_LIST_FAST(const struct ena_ring *r, unsigned n)
{
  struct ena_tx_buffer *const tbi = r->tx_buffer_info;
  uint16_t *const etr = r->empty_tx_reqs;
  const uint16_t inflight =
    (uint16_t)((r->next_to_use - r->next_to_clean) & r->size_mask);
  const unsigned window = n - inflight;
  uint64_t window_mask = 0;
  uint64_t free_mask = 0;
  unsigned bits = 0;

  for(unsigned k = 0; k < n; k++)
  {
    if(tbi[k].mbuf == NULL)
      free_mask |= (uint64_t)1 << k;
    else if(
      TX_DESCS(tbi[k]) < TX_DESCS_MIN ||
      TX_DESCS(tbi[k]) > ENA_MAX_TX_DESCS_PER_PKT)
      return false;
  }
  for(unsigned j = 0; j < n; j++)
  {
    if(j >= window)
      break;
    const uint16_t id = etr[(r->next_to_use + j) & r->size_mask];
    if(id >= n)
      return false;
    window_mask |= (uint64_t)1 << id;
  }
  for(unsigned k = 0; k < n; k++)
    if(free_mask & ((uint64_t)1 << k))
      bits++;
  return window_mask == free_mask && bits == window;
}

/*
 * INV_CLEAN: the MID-LOOP invariant of ena_tx_cleanup, generalised over the
 * loop's local cursor and accumulator.
 *
 * Why a separate predicate.  ena_tx_cleanup copies next_to_clean into a local,
 * advances the local on every completion, and writes the ring's field back
 * only after the loop (ena_ethdev.c:3207, 3252, 3258).  The same is true of
 * the descriptor accounting: ena_com_comp_ack is called once, at the end.  So
 * INV_FREE_LIST and INV_DESC_SUM are FALSE in the middle of the loop -- slots
 * have been freed but the ring's cursors do not know it yet -- and neither can
 * serve as a loop invariant.  INV_CLEAN is the version parameterised by the
 * local cursor `ntc` and the descriptors accumulated so far `acked`:
 *
 *   INV_CLEAN(r, n, r->next_to_clean, 0)  ==  INV_FREE_LIST and INV_DESC_SUM
 *
 * so it holds on entry, and after the loop writes the cursor back and acks
 * the descriptors it collapses to the two originals again.
 *
 * Stated with the bitmask form, so it is O(n) rather than O(n^2).
 */
static bool INV_CLEAN(
  const struct ena_ring *r,
  unsigned n,
  uint16_t ntc,
  uint16_t acked)
{
  struct ena_tx_buffer *const tbi = r->tx_buffer_info;
  uint16_t *const etr = r->empty_tx_reqs;
  const struct ena_com_io_sq *const sq = r->ena_com_io_sq;
  const uint16_t inflight = (uint16_t)((r->next_to_use - ntc) & r->size_mask);
  const unsigned window = n - inflight;
  uint64_t window_mask = 0;
  uint64_t free_mask = 0;
  unsigned bits = 0;
  /* 32-bit, deliberately.  Stated as a uint16_t sum the clause below would be
   * satisfiable by wrap-around: desc_sum 60000 and acked 5540 sum to 4 mod
   * 2^16, which passes the equality while leaving desc_sum far above the
   * queue depth, and INV_SQ_ACCOUNTING then fails after the ack.  Part 3 of
   * harness_cleanup_step.c found exactly that. */
  uint32_t desc_sum = 0;

  if(ntc > r->size_mask)
    return false;

  for(unsigned k = 0; k < n; k++)
  {
    if(tbi[k].mbuf == NULL)
      free_mask |= (uint64_t)1 << k;
    else
    {
      if(
        TX_DESCS(tbi[k]) < TX_DESCS_MIN ||
        TX_DESCS(tbi[k]) > ENA_MAX_TX_DESCS_PER_PKT)
        return false;
      desc_sum += TX_DESCS(tbi[k]);
    }
  }
  for(unsigned j = 0; j < n; j++)
  {
    if(j >= window)
      break;
    const uint16_t id = etr[(r->next_to_use + j) & r->size_mask];
    if(id >= n)
      return false;
    window_mask |= (uint64_t)1 << id;
  }
  for(unsigned k = 0; k < n; k++)
    if(free_mask & ((uint64_t)1 << k))
      bits++;
  if(!(window_mask == free_mask && bits == window))
    return false;

  /* The descriptors the device has not yet acknowledged are those of the
   * packets still in flight, plus those this cleanup has collected but not
   * yet acked.  The no-wrap bound is what keeps INV_SQ_ACCOUNTING true after
   * the ack: without it the equality alone admits a wrapped desc_sum. */
  if((uint32_t)desc_sum + (uint32_t)acked > (uint32_t)sq->q_depth - 1)
    return false;
  return (uint16_t)(sq->tail - sq->next_to_comp) ==
         (uint16_t)(desc_sum + acked);
}

#endif /* ENA_INV_H */
