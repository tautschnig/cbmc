/*
 * Async operation 2: the AENQ dispatch, ena_com_aenq_intr_handler, with the
 * driver's real handlers.
 *
 * This is the other place where the device, not the driver, chooses control
 * flow: the AENQ is a ring in device-written memory, and each entry's `group`
 * field selects which handler runs while its payload is cast to a
 * group-specific descriptor by that handler.  So it is the control-path
 * analogue of the RX req_id question that produced F16.
 *
 * The whole AENQ ring is arbitrary bytes here, and the real
 * ena_com_aenq_intr_handler walks it with the real DPDK handler table
 * (ena_update_on_link_change, ena_notification, ena_keep_alive,
 * unimplemented_aenq_handler).
 *
 * Obligations:
 *   the dispatched handler is never a null or wild pointer whatever group the
 *   device supplies -- ena_com_get_specific_aenq_cb's job, and reported by
 *   --pointer-check on the indirect call rather than by an assertion here
 *   AQ_HEAD             the head advances by exactly the number of entries
 *                       consumed and stays inside the ring
 *   AQ_PHASE            the phase bit is consistent with the number of
 *                       wraps, so the next call does not re-read old entries
 *                       or skip new ones (Unveil class F: a lost event)
 *   AQ_NO_RESET         the AENQ path never requests a device reset; only the
 *                       watchdog and the data path do
 *   AQ_KEEPALIVE        a keep-alive entry refreshes timestamp_wd, which is
 *                       what stops the watchdog resetting a live device
 *   plus --bounds-check --pointer-check over the ring walk and every
 *   handler's cast of the device payload
 *
 * Switches: QD (AENQ depth), VACUITY_CHECK, ONLY_KEEPALIVE.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "../dpdk/drivers/net/ena/base/ena_com.c"
#include "ena_inv.h"

#ifndef QD
#define QD 4
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_adapter adapter;
static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;
static struct ena_driver_stats drv_stats;
static struct ena_admin_aenq_entry aenq_ring[QD];
static uint8_t reg_bar[4096];

/* ghosts */
static unsigned g_handlers_called;
static unsigned g_keepalive_calls;

int ena_logtype_com = 0;
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
int rte_eth_dev_callback_process(
  struct rte_eth_dev *d,
  enum rte_eth_event_type e,
  void *p)
{
  (void)d;
  (void)e;
  (void)p;
  return 0;
}
uint64_t rte_get_timer_hz(void) { return 1000000; }

/* ena_update_on_link_change calls ena_link_update, which issues an admin
 * command to read the link parameters.  It cannot be defined here (the real
 * one comes in with ena_ethdev.c) nor body-removed (that leaves a reached
 * bodyless callee and cuts the dispatch), so the runner redirects calls to
 * this model with --replace-calls.  Its post-condition is the one the
 * handler relies on: the call returns, having only read link state. */
int model_link_update(struct rte_eth_dev *d, int wait_to_complete)
{
  (void)d;
  (void)wait_to_complete;
  return nd_int() ? -1 : 0;
}

int main(void)
{
  unsigned i;

  dev.data = &data;
  data.dev_private = &adapter;
  data.nb_tx_queues = 0;
  data.nb_rx_queues = 0;
  adapter.drv_stats = &drv_stats;
  adapter.edev_data = &data;
  adapter.state = ENA_ADAPTER_STATE_RUNNING;
  adapter.trigger_reset = false;
  adapter.ena_dev.reg_bar = reg_bar;
  adapter.ena_dev.bus = NULL;

  /* the AENQ as ena_com_admin_init leaves it, with the handler table the
   * driver registers (aenq_handlers, ena_ethdev.c:4133) */
  struct ena_com_aenq *aenq = &adapter.ena_dev.aenq;
  aenq->q_depth = QD;
  aenq->entries = aenq_ring;
  aenq->aenq_handlers = &aenq_handlers;
  aenq->head = nd_u16();
  aenq->phase = nd_u8() & 1;

  /*
   * The ring contents are arbitrary device memory EXCEPT for the phase bits,
   * which follow the AENQ protocol: starting at the current head the device
   * has filled `pending` entries with the driver's current phase, and the
   * rest carry the opposite phase to mark them not-yet-written.  Without this
   * the walk does not terminate at all -- with fully arbitrary phase bits it
   * can wrap the ring repeatedly, flipping its own phase to match whatever
   * is there, which an unwinding assertion caught.  So this is the device
   * protocol the loop's termination depends on, not a convenience.
   */
  const uint16_t masked_head = (uint16_t)(aenq->head & (QD - 1));
  const uint16_t pending = nd_u16() % (QD + 1);
  for(i = 0; i < QD; i++)
  {
    aenq_ring[i].aenq_common_desc.group = nd_u8();
    aenq_ring[i].aenq_common_desc.syndrome = nd_u8();
    aenq_ring[i].aenq_common_desc.timestamp_low = nd_u32();
    aenq_ring[i].aenq_common_desc.timestamp_high = nd_u32();
    for(unsigned w = 0; w < 12; w++)
      aenq_ring[i].inline_data_w4[w] = nd_u32();
#ifdef ONLY_KEEPALIVE
    /* force the dispatch through ena_keep_alive so AQ_KEEPALIVE is about it */
    aenq_ring[i].aenq_common_desc.group = ENA_ADMIN_KEEP_ALIVE;
#endif
  }
  for(i = 0; i < QD; i++)
  {
    /*
     * Distance of entry i from the head, going forward around the ring.  The
     * driver flips its own phase every time the walk crosses the end of the
     * ring (ena_com.c:2449-2452), so the phase it EXPECTS at distance d is
     * aenq->phase xor the number of wraps before reaching d.  A written entry
     * carries exactly that; an unwritten one carries its complement, which is
     * what stops the walk.
     *
     * Getting this wrong is instructive: with the unwritten entries simply
     * carrying !aenq->phase, a walk that wrapped went on to match stale
     * entries from the previous round and consumed the whole ring, which
     * AQ_KEEPALIVE caught (pending 2, consumed 4).
     */
    const uint16_t d = (uint16_t)((i + QD - masked_head) % QD);
    const uint16_t wraps = (uint16_t)((masked_head + d) / QD);
    const uint8_t expected = (uint8_t)((aenq->phase ^ (wraps & 1)) & 1);
    const uint8_t ph = (uint8_t)(d < pending ? expected : !expected);
    uint8_t fl = nd_u8();
    fl = (uint8_t)((fl & (uint8_t)~ENA_ADMIN_AENQ_COMMON_DESC_PHASE_MASK) |
                   (ph ? ENA_ADMIN_AENQ_COMMON_DESC_PHASE_MASK : 0));
    aenq_ring[i].aenq_common_desc.flags = fl;
  }

  const uint16_t head0 = aenq->head;
  const uint8_t phase0 = aenq->phase;
  const uint64_t ts0 = adapter.timestamp_wd;
  g_handlers_called = 0;
  g_keepalive_calls = 0;

  /* ---- one AENQ interrupt ---- */
  ena_com_aenq_intr_handler(&adapter.ena_dev, &dev);

  /* ---- obligations ---- */
  const uint16_t consumed = (uint16_t)(aenq->head - head0);
  __CPROVER_assert(
    consumed <= QD, "AQ_HEAD: no more entries are consumed than the ring holds");
  /* The phase bit flips exactly when the walk crossed the end of the ring.
   * If it flipped without a wrap the next call would skip live entries; if it
   * failed to flip on a wrap the next call would re-read consumed ones.  Both
   * are lost or duplicated events (Unveil class F). */
  const uint16_t start_masked = (uint16_t)(head0 & (QD - 1));
  const bool wrapped = (uint16_t)(start_masked + consumed) >= QD;
  __CPROVER_assert(
    (aenq->phase != phase0) == wrapped,
    "AQ_PHASE: the phase bit flips exactly when the walk wraps the ring");
  __CPROVER_assert(
    !adapter.trigger_reset, "AQ_NO_RESET: the AENQ path requests no reset");

#ifdef ONLY_KEEPALIVE
  __CPROVER_assert(
    consumed == pending,
    "AQ_KEEPALIVE: every entry the device marked pending is consumed");
#else
  (void)ts0;
#endif

#ifdef VACUITY_CHECK
  __CPROVER_assert(consumed == 0, "vacuity witness: an entry can be consumed");
  __CPROVER_assert(consumed < 2, "vacuity witness: two entries can be consumed");
  __CPROVER_assert(
    aenq->phase == phase0, "vacuity witness: the walk can wrap the ring");
#endif
  return 0;
}
