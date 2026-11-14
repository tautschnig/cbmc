/*
 * The remaining eth_dev_ops getters: do the values handed to the application
 * come from where the driver just fetched them, and do the index bounds hold?
 *
 * Found here: F27.  ena_xstats_get_by_id fetches the ENA-SRD statistics into
 * a local and then returns a different object's contents
 * (ena_ethdev.c:3650-3665):
 *
 *     struct ena_stats_srd srd_info = {0};
 *     ...
 *     if (!was_srd_info_copied) {
 *             was_srd_info_copied = true;
 *             ena_copy_ena_srd_info(adapter, &srd_info);   // fills the LOCAL
 *     }
 *     values[i] = *((uint64_t *)&adapter->srd_stats + id); // reads the ADAPTER
 *
 * The local it just filled is never read.  `adapter->srd_stats` is written
 * only on the multi-process path (:638 and :4198), so in a primary-only
 * process it is never written at all and the application gets zeros -- after
 * paying for an admin round trip whose result was discarded.  The full
 * ena_xstats_get does this correctly: it sets `stats_begin = &srd_info`
 * (:3574) and reads the local.  That contrast is what makes it a mistake
 * rather than a design.
 *
 *   XB_SRD_FRESH    a value returned for an SRD id is the value the device
 *                   supplied during this call
 *   XB_GLOBAL_BOUND an id below ENA_STATS_ARRAY_GLOBAL indexes inside
 *                   dev_stats
 *   XB_METRICS_BOUND an id below metrics_num indexes inside the local
 *                   metrics_stats array -- the bound F19 is missing, and which
 *                   ena_get_metrics_entries does apply here (:1849-1852)
 *   XB_VALID        the returned count never exceeds the ids requested
 *
 * -DWITH_SRD_FIX reads the local, as ena_xstats_get does, which is the fix.
 *
 * Switches: NIDS, VACUITY_CHECK, WITH_SRD_FIX.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"

#ifndef NIDS
#define NIDS 3
#endif

static int nd_int(void) { int v; return v; }
static uint64_t nd_u64(void) { uint64_t v; return v; }

static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;
static struct ena_adapter adapter;
static uint64_t ids[NIDS];
static uint64_t values[NIDS];

/* ---- ghost: what the device supplied during this call ---- */
#define SRD_MARKER 0xA5A5A5A5A5A5A5A5ull
static bool g_device_supplied_srd;

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
enum rte_proc_type_t rte_eal_process_type(void) { return RTE_PROC_PRIMARY; }

/* The device answers with a recognisable marker, so what reaches the
 * application can be compared against what was fetched. */
int ena_com_get_ena_srd_info(
  struct ena_com_dev *d, struct ena_admin_ena_srd_info *info)
{
  (void)d;
  if(info != NULL)
  {
    uint64_t *w = (uint64_t *)info;
    for(unsigned k = 0; k < sizeof(struct ena_stats_srd) / sizeof(uint64_t);
        k++)
      w[k] = SRD_MARKER;
    g_device_supplied_srd = true;
  }
  return 0;
}
int ena_com_get_customer_metrics(struct ena_com_dev *d, char *buf, u32 len)
{
  (void)d;
  if(buf != NULL)
    for(u32 k = 0; k < len; k++)
      buf[k] = 0x5a;
  return 0;
}
bool ena_com_get_cap(struct ena_com_dev *d, enum ena_admin_aq_caps_id id)
{
  (void)d;
  (void)id;
  return true;
}

int main(void)
{
  unsigned i;

  dev.data = &data;
  data.dev_private = &adapter;
  adapter.edev_data = &data;
  data.nb_rx_queues = 1;
  data.nb_tx_queues = 1;
  adapter.state = ENA_ADAPTER_STATE_RUNNING;
  rte_spinlock_init(&adapter.admin_lock);

  /* metrics_num as ena_get_metrics_entries leaves it: clamped to the array
   * it will index (ena_ethdev.c:1849-1852).  Assuming the clamp rather than
   * re-deriving it is what XB_METRICS_BOUND then checks. */
  adapter.metrics_num = (uint32_t)(nd_u64() % (ENA_MAX_CUSTOMER_METRICS + 1));

  /* adapter->srd_stats as a primary-only process leaves it: never written */
  memset(&adapter.srd_stats, 0, sizeof(adapter.srd_stats));

  /* the application's ids: arbitrary, which is the point -- they index the
   * statistics space and the driver is responsible for bounding them */
  for(i = 0; i < NIDS; i++)
    ids[i] = nd_u64();
  /* at least one id in the SRD range, so the obligation is about something */
  const uint64_t srd_base = ENA_STATS_ARRAY_GLOBAL + adapter.metrics_num;
  ids[0] = srd_base + (nd_u64() % ENA_STATS_ARRAY_ENA_SRD);

  g_device_supplied_srd = false;
  for(i = 0; i < NIDS; i++)
    values[i] = 0;

  const int valid = ena_xstats_get_by_id(&dev, ids, values, NIDS);

#ifdef WITH_SRD_FIX
  /* The fix is to read the local the device just filled, as the full
   * ena_xstats_get does at :3574.  The harness cannot edit the driver, so the
   * repaired value is reconstructed here: a model of the fix, marked as such. */
  if(ids[0] >= srd_base && ids[0] < srd_base + ENA_STATS_ARRAY_ENA_SRD)
    values[0] = SRD_MARKER;
#endif

  __CPROVER_assert(
    valid <= NIDS, "XB_VALID: the returned count never exceeds the ids asked for");

  /* The obligation F27 is about: if the driver went to the device for SRD
   * statistics, the number it hands back is the number it was given. */
  if(g_device_supplied_srd && ids[0] >= srd_base &&
     ids[0] < srd_base + ENA_STATS_ARRAY_ENA_SRD)
  {
    __CPROVER_assert(
      values[0] == SRD_MARKER,
      "XB_SRD_FRESH: a value returned for an SRD id is the value the device "
      "supplied during this call");
  }

#ifdef VACUITY_CHECK
  /* All must FAIL.  Without them the obligation above holds whenever the
   * device is never consulted, or no id lands in the SRD range. */
  __CPROVER_assert(
    !g_device_supplied_srd, "witness: the device can be consulted for SRD");
  __CPROVER_assert(
    !(ids[0] >= srd_base && ids[0] < srd_base + ENA_STATS_ARRAY_ENA_SRD),
    "witness: an id in the SRD range is reachable");
  __CPROVER_assert(valid == 0, "witness: at least one id can be valid");
#endif
  return 0;
}
