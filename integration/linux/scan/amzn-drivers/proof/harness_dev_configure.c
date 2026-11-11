/*
 * ena_dev_configure, the remaining device-lifecycle entry point.
 *
 * Small, but it establishes two things other proofs assumed:
 *
 *  1. missing_tx_completion_budget = RTE_MIN(ENA_MONITORED_TX_QUEUES,
 *     nb_tx_queues) (ena_ethdev.c:2499).  harness_watchdog.c ASSUMED that
 *     bound -- it is what makes check_for_tx_completions' loop stop before
 *     `qid = (qid + 1) % nb_tx_queues` can divide by zero, and what keeps
 *     tx_ring[qid] in bounds.  CFG_BUDGET discharges it.
 *
 *  2. active_aenq_groups is a subset of all_aenq_groups, the groups the
 *     device said it supports (ena_configure_aenq, :3908-3938).  The AENQ
 *     dispatch selects a handler by group, so the driver should never mark
 *     active a group the device never offered.
 *
 * Obligations:
 *   CFG_STATE          after configure the adapter is in CONFIG state
 *   CFG_BUDGET         the TX-completion budget never exceeds nb_tx_queues
 *   CFG_AENQ_SUBSET    on success, active_aenq_groups is a subset of the
 *                      device's all_aenq_groups
 *   CFG_AENQ_NO_LSC    if the application did not ask for link-state change
 *                      events, LINK_CHANGE is not left active
 *   CFG_AENQ_FAIL      if the device rejects the AENQ configuration, the
 *                      driver does not record the groups as active
 *
 * CFG_AENQ_FAIL is the error-path obligation: believing a group is active
 * when the device never accepted it would mean expecting events that never
 * arrive (Unveil class F).
 *
 * Switches: NQ, VACUITY_CHECK.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"

#ifndef NQ
#define NQ 2
#endif

static uint32_t nd_u32(void) { uint32_t v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_adapter adapter;
static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;

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

/* The AENQ configuration admin command.  The device may refuse it, which is
 * what CFG_AENQ_FAIL is about.  The real one arrives with ena_com.c, which
 * this harness does not compile in, so it is defined here. */
int ena_com_set_aenq_config(struct ena_com_dev *ena_dev, uint32_t groups)
{
  (void)ena_dev;
  (void)groups;
  return nd_int() ? ENA_COM_INVAL : 0;
}

int main(void)
{
  dev.data = &data;
  data.dev_private = &adapter;
  adapter.edev_data = &data;

  /* whatever the application configured */
  data.nb_tx_queues = nd_u16() % (NQ + 1); /* including zero TX queues */
  data.nb_rx_queues = nd_u16() % (NQ + 1);
  data.dev_conf.rxmode.mq_mode =
    nd_int() ? RTE_ETH_MQ_RX_RSS_FLAG : RTE_ETH_MQ_RX_NONE;
  data.dev_conf.intr_conf.lsc = nd_int() ? 1 : 0;

  /* what the device said it supports, and the previous state */
  adapter.all_aenq_groups = nd_u32();
  adapter.active_aenq_groups = nd_u32();
  adapter.missing_tx_completion_to = nd_u32();
  const uint32_t active_before = adapter.active_aenq_groups;

  int rc = ena_dev_configure(&dev);

  /* ---- obligations ---- */
  __CPROVER_assert(
    adapter.state == ENA_ADAPTER_STATE_CONFIG,
    "CFG_STATE: ena_dev_configure leaves the adapter in CONFIG state");
  __CPROVER_assert(
    adapter.missing_tx_completion_budget <= data.nb_tx_queues,
    "CFG_BUDGET: the TX-completion budget never exceeds nb_tx_queues");

  if(rc == 0)
  {
    __CPROVER_assert(
      (adapter.active_aenq_groups & ~adapter.all_aenq_groups) == 0,
      "CFG_AENQ_SUBSET: the active AENQ groups are a subset of what the "
      "device supports");
    __CPROVER_assert(
      data.dev_conf.intr_conf.lsc != 0 ||
        (adapter.active_aenq_groups & BIT(ENA_ADMIN_LINK_CHANGE)) == 0,
      "CFG_AENQ_NO_LSC: LINK_CHANGE is not left active unless the "
      "application asked for it");
  }
  else
  {
    __CPROVER_assert(
      adapter.active_aenq_groups == active_before,
      "CFG_AENQ_FAIL: a rejected AENQ configuration is not recorded as "
      "active");
  }

#ifdef VACUITY_CHECK
  __CPROVER_assert(rc != 0, "vacuity witness: configure can succeed");
  __CPROVER_assert(rc == 0, "vacuity witness: configure can fail");
  __CPROVER_assert(
    data.nb_tx_queues != 0, "vacuity witness: zero TX queues is reachable");
#endif
  return 0;
}
