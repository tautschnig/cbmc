/*
 * Async operation 1: the watchdog timer, ena_timer_wd_callback.
 *
 * This is the driver's own recovery mechanism, and F15 depends on it: a slot
 * holding a never-completed packet is detected here, which is what eventually
 * resets the device.  So it is worth knowing it is memory-safe and that a
 * reset it decides on is actually acted on.
 *
 * The timer fires asynchronously with respect to the data path, so the
 * adapter state it sees is whatever the data path left: every field it reads
 * is nondeterministic here, subject only to the per-queue invariants.
 *
 * Obligations:
 *   WD_QID        last_tx_comp_qid stays below nb_tx_queues, so the next
 *                 callback's tx_ring[qid] is in bounds
 *   WD_RESET_STICKY  the watchdog never CLEARS a pending reset: a reset
 *                 request must not be lost (Unveil class F)
 *   WD_EVENT      if this callback newly requests a reset, it also raises
 *                 RTE_ETH_EVENT_INTR_RESET, so the application learns of it
 *   WD_NO_FREE    the watchdog frees no packet: it only observes timestamps,
 *                 so it cannot race the data path into a double free
 *   plus --bounds-check --pointer-check over tx_ring[qid] and the per-slot
 *   timestamp scan
 *
 * Checked and NOT a bug: check_for_tx_completions does
 * `qid = (qid + 1) % nb_tx_queues` (ena_ethdev.c:2071) with no guard against
 * nb_tx_queues == 0, but the loop cannot run in that case, because
 * missing_tx_completion_budget is RTE_MIN(ENA_MONITORED_TX_QUEUES,
 * nb_tx_queues) (:2499) and `while (budget-- > 0)` is then false at once.
 * NQ_ZERO exercises that configuration so the claim is checked rather than
 * argued.
 *
 * Switches: RING, NQ, NQ_ZERO, VACUITY_CHECK.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING
#define RING 4
#endif
#ifndef NQ
#define NQ 2
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static uint64_t nd_u64(void) { uint64_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_adapter adapter;
static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;
static void *tx_queues[NQ];
static struct ena_tx_buffer pool_tbi[NQ][RING];
static uint16_t pool_etr[NQ][RING];
static uint8_t pool_push[NQ][96];
static struct rte_mbuf tx_mbuf[NQ][RING];
static struct ena_driver_stats drv_stats;
static uint32_t db_regs[2 * NQ];

/* ghosts */
static unsigned g_frees;          /* any packet freed by the watchdog */
static unsigned g_reset_events;   /* RTE_ETH_EVENT_INTR_RESET raised */

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

void rte_pktmbuf_free(struct rte_mbuf *m)
{
  (void)m;
  g_frees++;
}
void rte_mempool_put(struct rte_mempool *mp, void *o)
{
  (void)mp;
  (void)o;
  g_frees++;
}

int rte_eth_dev_callback_process(
  struct rte_eth_dev *d,
  enum rte_eth_event_type event,
  void *ret_param)
{
  (void)d;
  (void)ret_param;
  if(event == RTE_ETH_EVENT_INTR_RESET)
    g_reset_events++;
  return 0;
}

/* the device's view of its own admin queue: running or not, its choice */
bool ena_com_get_admin_running_state(struct ena_com_dev *d)
{
  (void)d;
  return nd_int() ? true : false;
}

int main(void)
{
  unsigned q, i;

  dev.data = &data;
  data.dev_private = &adapter;
  data.tx_queues = tx_queues;
  adapter.drv_stats = &drv_stats;
  adapter.edev_data = &data;

#ifdef NQ_ZERO
  /* the configuration in which the modulo would divide by zero */
  data.nb_tx_queues = 0;
  adapter.missing_tx_completion_budget = 0; /* RTE_MIN(_, 0) at configure */
#else
  data.nb_tx_queues = NQ;
  adapter.missing_tx_completion_budget = nd_u16() % (NQ + 1);
#endif
  data.nb_rx_queues = NQ;

  /* whatever the data path and any earlier callback left behind */
  adapter.state = ENA_ADAPTER_STATE_RUNNING;
  adapter.trigger_reset = nd_int() ? true : false;
  adapter.reset_reason = (enum ena_regs_reset_reason_types)(nd_u32() % 16);
  adapter.timestamp_wd = nd_u64();
  adapter.keep_alive_timeout = nd_u64();
  adapter.active_aenq_groups = nd_u32();
  adapter.missing_tx_completion_to = nd_u64();
  adapter.tx_cleanup_stall_delay = nd_u64();
  adapter.last_tx_comp_qid = nd_u16();
  /* the invariant the configure path establishes and the watchdog relies on */
  __CPROVER_assume(adapter.last_tx_comp_qid < data.nb_tx_queues + 1u);
#ifndef NQ_ZERO
  __CPROVER_assume(adapter.last_tx_comp_qid < data.nb_tx_queues);
#endif

  for(q = 0; q < NQ; q++)
  {
    struct ena_ring *t = &adapter.tx_ring[q];
    struct ena_com_io_sq *sq = &adapter.ena_dev.io_sq_queues[ENA_IO_TXQ_IDX(q)];
    struct ena_com_io_cq *cq = &adapter.ena_dev.io_cq_queues[ENA_IO_TXQ_IDX(q)];

    t->adapter = &adapter;
    t->type = ENA_RING_TYPE_TX;
    t->id = q;
    t->configured = 1;
    t->ring_size = RING;
    t->size_mask = RING - 1;
    t->sgl_size = ENA_PKT_MAX_BUFS;
    t->tx_buffer_info = pool_tbi[q];
    t->empty_tx_reqs = pool_etr[q];
    t->push_buf_intermediate_buf = pool_push[q];
    t->ena_com_io_sq = sq;
    t->ena_com_io_cq = cq;
    t->tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
    sq->mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
    sq->q_depth = RING;
    sq->db_addr = &db_regs[2 * q];
    sq->llq_info.descs_per_entry = nd_u16();
    sq->llq_info.descs_num_before_header = nd_u16();
    sq->llq_info.disable_meta_caching = false;
    sq->tail = nd_u16();
    sq->next_to_comp = nd_u16();
    __CPROVER_assume(INV_DEV_LLQ(sq));
    t->next_to_use = nd_u16() & (RING - 1);
    t->next_to_clean = nd_u16() & (RING - 1);
    t->last_cleanup_ticks = nd_u64();
    t->missing_tx_completion_threshold = nd_u16();
    tx_queues[q] = t;

    for(i = 0; i < RING; i++)
    {
      pool_tbi[q][i].mbuf = nd_int() ? &tx_mbuf[q][i] : NULL;
      pool_tbi[q][i].timestamp = nd_u64();
      pool_tbi[q][i].print_once = nd_int() ? true : false;
      pool_etr[q][i] = nd_u16();
      __CPROVER_assume(pool_etr[q][i] < RING);
    }
    __CPROVER_assume(INV_STARTED(t));
  }

  const bool reset_before = adapter.trigger_reset;
  g_frees = 0;
  g_reset_events = 0;

  /* ---- one watchdog tick ---- */
  ena_timer_wd_callback(NULL, &dev);

  /* ---- obligations ---- */
#ifndef NQ_ZERO
  __CPROVER_assert(
    adapter.last_tx_comp_qid < data.nb_tx_queues,
    "WD_QID: last_tx_comp_qid stays in range for the next tick");
#endif
  __CPROVER_assert(
    !reset_before || adapter.trigger_reset,
    "WD_RESET_STICKY: the watchdog never clears a pending reset");
  __CPROVER_assert(
    reset_before || !adapter.trigger_reset || g_reset_events >= 1,
    "WD_EVENT: a newly requested reset is reported to the application");
  __CPROVER_assert(
    g_frees == 0, "WD_NO_FREE: the watchdog frees no packet");

#ifdef VACUITY_CHECK
  __CPROVER_assert(
    !adapter.trigger_reset, "vacuity witness: the watchdog can request a reset");
  __CPROVER_assert(
    g_reset_events == 0, "vacuity witness: a reset event can be raised");
  __CPROVER_assert(
    reset_before, "vacuity witness: the tick can start with no reset pending");
#endif
  return 0;
}
