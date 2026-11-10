/*
 * F19: ena_rss_reta_query reads past a stack array when reta_size exceeds the
 * indirection-table size.
 *
 * ena_rss_reta_update checks the size (ena_rss.c:84-89):
 *
 *     if (reta_size > ENA_RX_RSS_TABLE_SIZE) { ...; return -EINVAL; }
 *
 * ena_rss_reta_query does not.  It declares
 *
 *     uint32_t indirect_table[ENA_RX_RSS_TABLE_SIZE];     (ena_rss.c:132)
 *
 * fills it from the device, and then copies out
 *
 *     for (i = 0; i < reta_size; i++) { ...
 *             reta_conf[..].reta[..] = ENA_IO_RXQ_IDX_REV(indirect_table[i]);
 *     }                                                   (ena_rss.c:156-162)
 *
 * so for reta_size > ENA_RX_RSS_TABLE_SIZE (128) it reads off the end of a
 * 128-entry stack array and copies whatever is there into the caller's
 * buffer: an out-of-bounds stack read and a disclosure of stack contents to
 * the application.
 *
 * The ethdev layer does not bound it either.  rte_eth_dev_rss_reta_query
 * (rte_ethdev.c:4773) only rejects a NULL config and calls
 * eth_check_reta_mask (:4677), which merely requires one mask bit to be set;
 * neither compares reta_size against dev_info.reta_size, which ENA reports as
 * ENA_RX_RSS_TABLE_SIZE (ena_ethdev.c:2646).
 *
 * The obligation is that every read of indirect_table is in bounds.  The
 * harness makes that observable by giving indirect_table a guard region: the
 * model of ena_mp_indirect_table_get fills only the real 128 entries, and
 * --bounds-check decides the rest.
 *
 * -DWITH_UPDATE_CHECK adds the bound ena_rss_reta_update already has, which
 * is the proposed fix, and the obligation then holds.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "../dpdk/drivers/net/ena/ena_rss.c"

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static int nd_int(void) { int v; return v; }

#define GROUPS 8 /* enough reta_conf groups for reta_size up to 8*64 = 512 */

static struct ena_adapter adapter;
static struct rte_eth_dev dev;
static struct rte_eth_dev_data data;
static struct rte_eth_rss_reta_entry64 reta_conf[GROUPS];

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

/* The device's indirection table, read through the multi-process proxy.  It
 * fills exactly ENA_RX_RSS_TABLE_SIZE entries, which is the contract: the
 * array the caller passes is declared with that size.  The real function
 * arrives with ena_rss.c, so the runner redirects calls here with
 * --replace-calls. */
int model_indirect_table_get(
  struct ena_adapter *adapter_arg,
  uint32_t *table)
{
  (void)adapter_arg;
  if(nd_int())
    return -1;
  for(unsigned i = 0; i < ENA_RX_RSS_TABLE_SIZE; i++)
    table[i] = nd_u32() % (2 * ENA_RX_RSS_TABLE_SIZE);
  return 0;
}

int main(void)
{
  dev.data = &data;
  data.dev_private = &adapter;
  data.port_id = 0;
  data.nb_rx_queues = 1;
  /* RSS must be configured, or the function returns -ENOTSUP first */
  data.dev_conf.rxmode.offloads = RTE_ETH_RX_OFFLOAD_RSS_HASH;

  /* what the application passes.  rte_eth_dev_rss_reta_query requires a
   * non-NULL config and at least one mask bit; it does NOT bound the size. */
  uint16_t reta_size = nd_u16();
  __CPROVER_assume(reta_size >= 1 && reta_size <= GROUPS * RTE_ETH_RETA_GROUP_SIZE);
#ifdef WITH_UPDATE_CHECK
  /* the proposed fix: the same bound ena_rss_reta_update applies */
  __CPROVER_assume(reta_size <= ENA_RX_RSS_TABLE_SIZE);
#endif
  for(unsigned g = 0; g < GROUPS; g++)
    reta_conf[g].mask = nd_u32();

  int rc = ena_rss_reta_query(&dev, reta_conf, reta_size);
  (void)rc;

#ifdef VACUITY_CHECK
  /* both must FAIL */
  __CPROVER_assert(rc != 0, "vacuity witness: the query can succeed");
  __CPROVER_assert(
    reta_size <= ENA_RX_RSS_TABLE_SIZE,
    "vacuity witness: an oversized reta_size reaches the driver");
#endif
  return 0;
}
