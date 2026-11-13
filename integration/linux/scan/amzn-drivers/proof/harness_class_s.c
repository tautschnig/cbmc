/*
 * Class S for the DPDK setters: does the return code reflect what the device
 * actually did?
 *
 * The class is the largest group the Unveil class list had no name for -- an
 * operation the device REJECTED reported to the caller as SUCCESS -- and six
 * of the forty class Z findings are this shape.  Two obligations of it already
 * existed in this corpus (F18_SETUP_RC, CFG_AENQ_FAIL); this harness applies
 * the same shape to the entry points that had none.
 *
 * This is the counterpart of fbsd/harness_fbsd_mtu.c, which shows FreeBSD's
 * SIOCSIFMTU reporting success for a rejected MTU because ena_ioctl discards
 * ena_change_mtu's result (ena.c:2426).  DPDK propagates at both layers --
 * ena_mtu_set returns the device's rc directly (ena_ethdev.c:1297-1303), and
 * rte_eth_dev_set_mtu only publishes dev->data->mtu when the driver returned
 * 0 (rte_ethdev.c:4190-4192) -- so the obligations should HOLD here.  A
 * contrast pair is worth more than either half alone: the same obligation,
 * one driver failing it and one passing.
 *
 *   SM_MTU_RC        ena_mtu_set returns 0 only if the device accepted
 *   SM_MTU_PUBLISH   the MTU is published to the application only when the
 *                    driver returned 0 (the ethdev layer's half, modelled)
 *   SM_RETA_RC       ena_rss_reta_update returns 0 only if every entry the
 *                    device was asked for was accepted
 *   SM_INTR_RC       ena_rx_queue_intr_enable returns 0 only if the queue
 *                    was in a state where unmasking means anything (F17)
 *
 * -DENTRY selects which setter is exercised.
 * Switches: ENTRY, VACUITY_CHECK.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "../dpdk/drivers/net/ena/ena_rss.c"
#include "ena_inv.h"

#ifndef ENTRY
#  define ENTRY 0
#endif
#define NRETA 2
#define MAX_MTU 9000

static int nd_int(void) { int v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint64_t nd_u64(void) { uint64_t v; return v; }

static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;
static struct ena_adapter adapter;
static struct rte_eth_rss_reta_entry64 reta[NRETA];

/* ---- ghost state: what the device was asked and what it answered ---- */
static unsigned g_asked;
static unsigned g_refused;
static uint16_t g_published_mtu;

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

/* ENA_PROXY routes through the multi-process machinery.  This harness is the
 * primary process, so the request-reply round trip is not taken, but the
 * EAL primitives still need bodies: a REACHED bodyless callee cuts the path
 * and would leave the obligations holding for want of one. */
enum rte_proc_type_t rte_eal_process_type(void) { return RTE_PROC_PRIMARY; }
int rte_mp_request_sync(
  struct rte_mp_msg *req, struct rte_mp_reply *reply, const struct timespec *ts)
{
  (void)req;
  (void)ts;
  reply->nb_received = 0;
  reply->nb_sent = 0;
  reply->msgs = NULL;
  return nd_int();
}
const char *rte_strerror(int e)
{
  (void)e;
  return "";
}

/* The device's answer is the whole point, so it is nondeterministic: a device
 * may refuse a request the driver considers legal. */
int ena_com_set_dev_mtu(struct ena_com_dev *d, uint32_t mtu)
{
  (void)d;
  (void)mtu;
  g_asked++;
  if(nd_int())
  {
    g_refused++;
    return ENA_COM_INVAL;
  }
  return 0;
}
int ena_com_indirect_table_fill_entry(struct ena_com_dev *d, u16 i, u16 v)
{
  (void)d;
  (void)i;
  (void)v;
  g_asked++;
  if(nd_int())
  {
    g_refused++;
    return ENA_COM_INVAL;
  }
  return 0;
}
int ena_com_indirect_table_set(struct ena_com_dev *d)
{
  (void)d;
  g_asked++;
  if(nd_int())
  {
    g_refused++;
    return ENA_COM_INVAL;
  }
  return 0;
}
int ena_com_indirect_table_get(struct ena_com_dev *d, u32 *t)
{
  (void)d;
  if(t != NULL)
    for(unsigned i = 0; i < ENA_RX_RSS_TABLE_SIZE; i++)
      t[i] = 0;
  return nd_int();
}
/* the interrupt teardown ena_stop performs; not what is under test here */
int rte_intr_disable(const struct rte_intr_handle *h)
{
  (void)h;
  return 0;
}
int rte_intr_enable(const struct rte_intr_handle *h)
{
  (void)h;
  return 0;
}
int rte_intr_efd_disable(struct rte_intr_handle *h)
{
  (void)h;
  return 0;
}
void rte_intr_vec_list_free(struct rte_intr_handle *h) { (void)h; }
void rte_timer_stop_sync(struct rte_timer *t) { (void)t; }

int ena_com_dev_reset(struct ena_com_dev *d, enum ena_regs_reset_reason_types r)
{
  (void)d;
  (void)r;
  g_asked++;
  if(nd_int())
  {
    g_refused++;
    return ENA_COM_TIMER_EXPIRED;
  }
  return 0;
}
void ena_com_unmask_intr(struct ena_com_io_cq *cq, struct ena_eth_io_intr_reg *r)
{
  (void)cq;
  (void)r;
  g_asked++;
}

int main(void)
{
  dev.data = &data;
  data.dev_private = &adapter;
  adapter.edev_data = &data;
  data.nb_rx_queues = 2;
  data.nb_tx_queues = 2;
  adapter.state = ENA_ADAPTER_STATE_RUNNING;
  adapter.max_mtu = MAX_MTU;
  data.dev_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_RSS_HASH;
  g_asked = 0;
  g_refused = 0;

  int rc = 0;

#if ENTRY == 0
  /* ---- mtu_set ---- */
  const uint16_t mtu = nd_u16();
  g_published_mtu = nd_u16();
  const uint16_t published_before = g_published_mtu;
  rc = ena_mtu_set(&dev, mtu);
  /* The ethdev layer's half, from rte_ethdev.c:4190-4192: the MTU reaches the
   * application only when the driver returned 0.  Modelled because
   * rte_ethdev.c is not part of this build. */
  if(rc == 0)
    g_published_mtu = mtu;

  __CPROVER_assert(
    rc != 0 || g_refused == 0,
    "SM_MTU_RC: ena_mtu_set returns 0 only if the device accepted the MTU");
  __CPROVER_assert(
    rc == 0 || g_published_mtu == published_before,
    "SM_MTU_PUBLISH: a refused MTU is not published to the application");
#elif ENTRY == 1
  /* ---- reta_update ---- */
  uint16_t reta_size = nd_u16();
  __CPROVER_assume(reta_size <= NRETA * RTE_ETH_RETA_GROUP_SIZE);
  __CPROVER_assume(reta_size <= ENA_RX_RSS_TABLE_SIZE);
  for(unsigned i = 0; i < NRETA; i++)
  {
    reta[i].mask = nd_u64();
    for(unsigned j = 0; j < RTE_ETH_RETA_GROUP_SIZE; j++)
      reta[i].reta[j] = nd_u16() % 2;
  }
  rc = ena_rss_reta_update(&dev, reta, reta_size);
  __CPROVER_assert(
    rc != 0 || g_refused == 0,
    "SM_RETA_RC: ena_rss_reta_update returns 0 only if the device accepted "
    "every entry it was asked for");
#elif ENTRY == 3
  /* ---- dev_stop ----
   *
   * ena_stop asks the device to reset when a reset was requested, logs any
   * failure, and then returns 0 unconditionally (ena_ethdev.c:1382-1405):
   *
   *     if (adapter->trigger_reset) {
   *             rc = ena_com_dev_reset(ena_dev, adapter->reset_reason);
   *             if (rc)
   *                     PMD_DRV_LOG_LINE(ERR, "Device reset failed, rc: %d", rc);
   *     }
   *     ...
   *     return 0;
   *
   * It then disables the interrupt, frees the vector list and marks every
   * queue STOPPED.  So the application is told the port stopped cleanly while
   * the device may still be running. */
  /* rte_intr_handle is opaque in the public API, so it cannot be declared
   * directly; a byte array stands in, as in harness_setup_rx_intr.c.  Nothing
   * here dereferences it -- the rte_intr_* calls are modelled. */
  static struct rte_pci_device stop_pci;
  static uint8_t stop_handle_storage[256];
  stop_pci.intr_handle = (struct rte_intr_handle *)stop_handle_storage;
  dev.device = &stop_pci.device;
  adapter.trigger_reset = true;
  adapter.reset_reason = ENA_REGS_RESET_OS_TRIGGER;
  data.nb_rx_queues = 0;
  data.nb_tx_queues = 0;
  rc = ena_stop(&dev);
  __CPROVER_assert(
    rc != 0 || g_refused == 0,
    "SM_STOP_RC: ena_stop returns 0 only if a requested device reset "
    "actually succeeded");
#else
  /* ---- rx_queue_intr_enable ----
   *
   * ena_rx_queue_intr_enable returns 0 unconditionally: the function it calls,
   * ena_rx_queue_intr_set, is void, so there is nothing to propagate
   * (ena_ethdev.c:3892-3898).  That makes F17 a class S finding as well as a
   * device-access one -- the caller is told the interrupt is enabled whether
   * or not the queue was ever started. */
  adapter.rx_ring[0].configured = 1;
  adapter.rx_ring[0].ena_com_io_cq = &adapter.ena_dev.io_cq_queues[0];
  data.rx_queues[0] = &adapter.rx_ring[0];
  /* a configured queue may or may not have been started */
  const bool started = nd_int() ? true : false;
  adapter.rx_ring[0].ena_com_io_cq->q_depth = started ? 4 : 0;
  rc = ena_rx_queue_intr_enable(&dev, 0);
  __CPROVER_assert(
    rc != 0 || started,
    "SM_INTR_RC: ena_rx_queue_intr_enable returns 0 only if the queue was "
    "started, so that unmasking means something");
#endif

#ifdef VACUITY_CHECK
  /* Both must FAIL.  Without them an obligation of the form "rc != 0 or the
   * device was not refused" holds whenever the device is never asked, or
   * never refuses. */
  __CPROVER_assert(g_asked == 0, "witness: the device can be asked");
#  if ENTRY != 2
  __CPROVER_assert(g_refused == 0, "witness: the device can refuse");
#  endif
#  if ENTRY == 2
  __CPROVER_assert(started, "witness: an unstarted queue is reachable");
#  endif
#endif
  (void)rc;
  return 0;
}
