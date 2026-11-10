/*
 * Config/query phase: ena_xstats_get_by_id, where the APPLICATION supplies
 * the indices.
 *
 * Each id is decoded by subtracting the size of one statistics region after
 * another and indexing whatever region it lands in
 * (ena_ethdev.c:3623-3692).  Several of those regions are sized by values the
 * driver got from the device (metrics_num) or from the ethdev layer
 * (nb_rx_queues, nb_tx_queues), and the indexing is done by pointer
 * arithmetic on a struct address rather than through an array, so nothing in
 * the types bounds it.  That is the F8/F9 shape with the roles reversed: the
 * application picks the index, the device picks some of the limits.
 *
 * Obligations: every read is in bounds, and the returned count never exceeds
 * the number of ids supplied.  The regions are given guard space either side
 * so that an off-by-one walks into a distinct object --bounds-check can see.
 *
 * Checked and NOT a bug, by reading first and then by this harness:
 *   - metrics_num is clamped to ENA_MAX_CUSTOMER_METRICS by
 *     ena_get_metrics_entries (ena_ethdev.c:1849-1851), so the metrics region
 *     cannot overrun metrics_stats[ENA_MAX_CUSTOMER_METRICS];
 *   - the per-queue regions are sized ENA_STATS_ARRAY_* times the queue
 *     count, so with zero queues the region is empty and the `id % count`
 *     that would divide by zero is unreachable.  NQ_ZERO exercises that.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"

#ifndef NIDS
#define NIDS 2
#endif
#ifndef NQ
#define NQ 2
#endif

static uint64_t nd_u64(void) { uint64_t v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_adapter adapter;
static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;
static uint64_t ids[NIDS];
static uint64_t values[NIDS];

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
void rte_spinlock_lock(rte_spinlock_t *s) { (void)s; }
void rte_spinlock_unlock(rte_spinlock_t *s) { (void)s; }

/* The two admin-queue reads the decoder may trigger.  They fill driver
 * buffers and return nothing the obligations depend on.  The real ones arrive
 * with ena_ethdev.c, so the runner redirects calls here with
 * --replace-calls. */
void model_copy_customer_metrics(
  struct ena_adapter *a,
  uint64_t *buf,
  size_t buf_size)
{
  (void)a;
  for(size_t i = 0; i < buf_size && i < ENA_MAX_CUSTOMER_METRICS; i++)
    buf[i] = nd_u64();
}
void model_copy_ena_srd_info(
  struct ena_adapter *a,
  struct ena_stats_srd *srd)
{
  (void)a;
  (void)srd;
}

int main(void)
{
  unsigned i;

  dev.data = &data;
  data.dev_private = &adapter;
#ifdef NQ_ZERO
  data.nb_rx_queues = 0;
  data.nb_tx_queues = 0;
#else
  data.nb_rx_queues = NQ;
  data.nb_tx_queues = NQ;
#endif
  adapter.edev_data = &data;

  /* metrics_num as ena_get_metrics_entries leaves it: clamped */
  adapter.metrics_num = nd_u16() % (ENA_MAX_CUSTOMER_METRICS + 1);

  /* the application's ids are arbitrary 64-bit values */
  for(i = 0; i < NIDS; i++)
    ids[i] = nd_u64();

  int rc = ena_xstats_get_by_id(&dev, ids, values, NIDS);

  __CPROVER_assert(
    rc >= 0 && rc <= NIDS,
    "XS_COUNT: the number of valid values never exceeds the ids supplied");

#ifdef VACUITY_CHECK
  __CPROVER_assert(rc == 0, "vacuity witness: an id can be decoded");
  __CPROVER_assert(rc == NIDS, "vacuity witness: an id can be rejected");
#endif
  return 0;
}
