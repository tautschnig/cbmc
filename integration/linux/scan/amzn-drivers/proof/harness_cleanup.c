/*
 * The second operation of the induction: ena_tx_cleanup.
 *
 * Why this matters.  With only xmit, "all operation sequences" is a sequence
 * of one operation type, which is not a sequence at all.  Cleanup is also
 * where the classes our taint pipeline cannot express live: it is the only
 * place the TX path frees an mbuf, so double-free, use-after-free and leaked
 * descriptors are decided here.  Those are 25% of the Unveil findings.
 *
 * Shape: from an arbitrary INV_STARTED state with arbitrary slot occupancy,
 * one ena_tx_cleanup call must re-establish INV_STARTED and respect the
 * ownership discipline.  Composed with the xmit step over the same invariant
 * (ena_inv.h), this is the induction step for the two-operation alphabet.
 *
 * The cleanup loop is bounded by cleanup_budget <= size_mask, i.e. by the
 * ring size, which is concrete here.  So it is discharged by unwinding with
 * --unwinding-assertions and needs no loop contract -- which keeps this proof
 * clear of DFCC issues B and C entirely.
 *
 * DEVICE TRUST.  req_id comes from the completion queue, which the device
 * writes, so ena_com_tx_comp_req_id_get is modelled as returning an arbitrary
 * req_id or "no more completions".  That is the sound model: the proof then
 * covers a device that replays, reorders or invents completions.  Note the
 * driver does validate it (validate_tx_req_id checks both req_id < ring_size
 * and tx_info->mbuf != NULL), unlike the EFA kverbs paths in F4/F5, so this
 * harness is checking a guard that exists rather than hunting a missing one.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING_SIZE
#define RING_SIZE 8
#endif
#ifndef MBUF_POOL
#define MBUF_POOL RING_SIZE
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static int nd_int(void) { int v; return v; }

/* Two arbitrary slot indices: asserting a two-slot property at a pair of
 * nondeterministic indices is universal quantification over pairs. */
uint16_t g_p1, g_p2;

/* ---------------- ghost state for the ownership discipline -------------- */

/* An arbitrary mbuf to watch.  Watching one nondeterministically chosen mbuf
 * and proving the property for it is universal quantification over mbufs, and
 * it costs O(1) instead of a counter per pool entry. */
static struct rte_mbuf *g_watch;
static unsigned g_free_calls; /* ghost: how often g_watch was freed */

static struct rte_mbuf pool_mbuf[MBUF_POOL];
static struct ena_tx_buffer pool_tbi[RING_SIZE];
static uint16_t pool_etr[RING_SIZE];
static uint8_t pool_push[96];
static struct ena_com_io_sq the_sq;
static struct ena_com_io_cq the_cq;
static uint32_t the_db_reg;

/*
 * The only place the TX path releases an mbuf.  Instrumented rather than
 * stubbed: the FREE_ONCE obligation is checked here, at the moment of the
 * free, because that is the only point where double-freeing is observable.
 */
void rte_pktmbuf_free(struct rte_mbuf *m)
{
  if(m == NULL)
    return;

  __CPROVER_assert(
    m != g_watch || g_free_calls == 0,
    "FREE_ONCE: no mbuf is released twice");

  if(m == g_watch)
    g_free_calls++;
}

void rte_pktmbuf_free_bulk(struct rte_mbuf **mbufs, unsigned int count)
{
  unsigned int i;

  for(i = 0; i < count; i++)
    rte_pktmbuf_free(mbufs[i]);
}

/*
 * The device side of the completion queue.  Every completion is
 * device-controlled, so the req_id is arbitrary and the device may stop
 * reporting at any point.
 */
int ena_com_tx_comp_req_id_get(struct ena_com_io_cq *io_cq, uint16_t *req_id)
{
  (void)io_cq;
  if(nd_int())
    return ENA_COM_TRY_AGAIN; /* no more completions pending */
  *req_id = nd_u16();         /* ARBITRARY: the device chooses */
  return 0;
}

/* DPDK logging: variadic, and it drags vsnprintf in with symbolic arguments
 * for no effect on ring state. */
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
void rte_pci_register(struct rte_pci_driver *driver)
{
  (void)driver;
}

/* The per-slot invariant, stated for one slot (see ena_inv.h for why the
 * quantifier is handled by probing a nondeterministic index). */
static bool INV_SLOT(const struct ena_ring *r, uint16_t p)
{
  uint16_t *etr = r->empty_tx_reqs;
  struct ena_tx_buffer *tbi = r->tx_buffer_info;

  if(p >= r->ring_size)
    return true;
  return etr[p] < r->ring_size && tbi[p].num_of_bufs <= ENA_PKT_MAX_BUFS;
}

/* No mbuf is owned by two slots: a violation is a double free on cleanup. */
static bool OWN_UNIQUE(const struct ena_ring *r, uint16_t p1, uint16_t p2)
{
  struct ena_tx_buffer *tbi = r->tx_buffer_info;

  if(p1 >= r->ring_size || p2 >= r->ring_size || p1 == p2)
    return true;
  return !(tbi[p1].mbuf != NULL && tbi[p1].mbuf == tbi[p2].mbuf);
}

int main(void)
{
  static struct ena_adapter adapter;
  struct ena_ring ring;
  unsigned i;
  uint32_t free_pkt_cnt;
  int rc;

  /* Not havoc_object on the adapter: it embeds function pointers, and
   * havocking them lets CBMC's function-pointer removal admit the whole
   * dev_ops table as a call target. */
  __CPROVER_havoc_object(&ring);
  __CPROVER_havoc_object(&the_sq.llq_info);

  /* ---- an arbitrary started TX queue ---- */
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
  ring.ena_com_io_sq = &the_sq;
  ring.ena_com_io_cq = &the_cq;
  the_sq.db_addr = &the_db_reg;
  the_sq.q_depth = RING_SIZE;
  /* LLQ mode, as INV_STARTED requires (the mode the xmit step covers) */
  ring.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  the_sq.mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;

  ring.next_to_use = nd_u16() & (RING_SIZE - 1);
  ring.next_to_clean = nd_u16() & (RING_SIZE - 1);
  the_sq.tail = nd_u16();
  the_sq.next_to_comp = nd_u16();

  /* both the fast-free and the per-packet release path are covered */
  ring.offloads = nd_u32();

  /* ---- arbitrary slot occupancy, consistent with the invariant ---- */
  for(i = 0; i < MBUF_POOL; i++)
  {
    pool_mbuf[i].nb_segs = nd_u16();
    __CPROVER_assume(pool_mbuf[i].nb_segs >= 1 && pool_mbuf[i].nb_segs <= 4);
  }
  for(i = 0; i < RING_SIZE; i++)
  {
    /* Each occupied slot owns its OWN mbuf.  Letting slots choose freely from
     * a smaller pool makes ownership uniqueness impossible by pigeonhole --
     * 8 slots cannot hold 2 distinct mbufs -- so the harness would be
     * refuting its own setup rather than the driver.  Distinct mbufs are also
     * the real situation: each in-flight packet is a separate mbuf, which is
     * precisely the application obligation the xmit step had to assume. */
    pool_tbi[i].mbuf = nd_int() ? &pool_mbuf[i] : NULL;
    pool_tbi[i].num_of_bufs = nd_u16();
    __CPROVER_assume(pool_tbi[i].num_of_bufs <= ENA_PKT_MAX_BUFS);
    pool_tbi[i].tx_descs = nd_u32(); /* any unsigned, read mod 2^16 */
    __CPROVER_assume(TX_DESCS(pool_tbi[i]) <= ENA_MAX_TX_DESCS_PER_PKT);
    pool_etr[i] = nd_u16();
    __CPROVER_assume(pool_etr[i] < RING_SIZE);
  }

  g_p1 = nd_u16();
  g_p2 = nd_u16();
  g_watch = &pool_mbuf[nd_u16() % RING_SIZE];
  g_free_calls = 0;

  /*
   * INV_DESC_SUM, discovered by proof failure.  The descriptors outstanding
   * on the SQ are exactly the descriptors of the packets still in flight:
   *
   *   (uint16_t)(tail - next_to_comp) == sum of tx_descs over occupied slots
   *
   * Without it the harness admits states the driver can never reach -- slots
   * claiming descriptors while the SQ says nothing is outstanding -- and
   * ena_com_comp_ack then drives next_to_comp past tail, breaking
   * INV_SQ_ACCOUNTING.  The counterexample was tail == next_to_comp == 32752
   * with total_tx_descs == 16, giving next_to_comp == 32768 and an apparent
   * in-flight count of 65520 against a q_depth of 8.
   *
   * It is a sum, so it cannot be stated by probing one index; the ring size
   * is concrete here, so the harness computes it directly.
   */
#ifndef MEASURE_NO_DESC_SUM
  {
    uint16_t desc_sum = 0;

    for(i = 0; i < RING_SIZE; i++)
      if(pool_tbi[i].mbuf != NULL)
        desc_sum = desc_sum + TX_DESCS(pool_tbi[i]);

    __CPROVER_assume(
      (uint16_t)(the_sq.tail - the_sq.next_to_comp) == desc_sum);
  }
#endif /* MEASURE_NO_DESC_SUM: for cost measurement only.  Without the
        * assumption INV_STARTED is known to FAIL (see the INV_DESC_SUM
        * note), so a build with this switch proves nothing. */

  __CPROVER_assume(INV_STARTED(&ring));
#ifndef NO_FREE_LIST
  __CPROVER_assume(INV_FREE_LIST(&ring, RING_SIZE));
#endif
  __CPROVER_assume(INV_SLOT(&ring, g_p1));
  __CPROVER_assume(OWN_UNIQUE(&ring, g_p1, g_p2));
  __CPROVER_assume(INV_DEV_LLQ(&the_sq));

  free_pkt_cnt = nd_u32();

  /* ---- one arbitrary cleanup operation ---- */
  rc = ena_tx_cleanup((void *)&ring, free_pkt_cnt);
  (void)rc;

  /* ---- obligations ---- */
#if VACUITY_CHECK
  /* must FAIL: if it passes, no completion is ever processed and every
   * obligation below is about a no-op */
  __CPROVER_assert(g_free_calls == 0, "vacuity witness: a packet can be freed");
#endif

  __CPROVER_assert(
    INV_STARTED(&ring), "INV_STARTED preserved by ena_tx_cleanup");
  {
    /* The cleanup step ASSUMES INV_DESC_SUM, so it must also preserve it, or
     * a second cleanup could start from a state the proof never covered. */
    uint16_t sum_after = 0;
    for(i = 0; i < RING_SIZE; i++)
      if(pool_tbi[i].mbuf != NULL)
        sum_after = (uint16_t)(sum_after + TX_DESCS(pool_tbi[i]));
    __CPROVER_assert(
      (uint16_t)(the_sq.tail - the_sq.next_to_comp) == sum_after,
      "INV_DESC_SUM preserved by ena_tx_cleanup");
  }
#ifndef NO_FREE_LIST
  /* INV_FREE_LIST is a permutation property over the whole ring; evaluating it
   * twice (assume and assert) is the dominant cost at ring 8 even under the
   * loop contract, so -DNO_FREE_LIST splits it off.  Dropping it WEAKENS the
   * pre-state too, so the remaining obligations are proven from LESS, not
   * more: the split is sound in that direction. */
  __CPROVER_assert(
    INV_FREE_LIST(&ring, RING_SIZE), "INV_FREE_LIST preserved by ena_tx_cleanup");
#endif
  __CPROVER_assert(
    INV_SLOT(&ring, g_p1), "INV_SLOT preserved for an arbitrary ring slot");
  __CPROVER_assert(
    OWN_UNIQUE(&ring, g_p1, g_p2),
    "OWN_UNIQUE: no mbuf owned by two slots after cleanup");

  /*
   * The use-after-free clause.  Releasing an mbuf while a slot still points
   * at it is the driver-side precondition for every UAF in this class: a
   * later cleanup or a reset path would hand the same pointer to the mempool
   * again.  Stating it at a nondeterministic slot quantifies over slots.
   */
  __CPROVER_assert(
    g_free_calls == 0 || g_p1 >= ring.ring_size ||
      ring.tx_buffer_info[g_p1].mbuf != g_watch,
    "OWN_NO_DANGLING: no slot still owns a released mbuf");

  return 0;
}
