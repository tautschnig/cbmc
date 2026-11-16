// Linux ENA: `ethtool -L` on an interface that is not up, and what a failure
// leaves behind for the next `ethtool -X`.
//
// Unveil (Linux ENA):
//   K  "ethtool -L on a never-up interface fails with -EINVAL but the new
//       count sticks"
//   H  "NULL dereference oops on ethtool -X after a failed ethtool -L on a
//       down interface"
//
// Real code: ena_set_channels, ena_update_queue_count, ena_close,
// ena_init_io_rings, ena_rss_configure, ena_rss_init_default, the ena_com RSS
// functions (ena_com_rss_init/destroy, indirect table fill/set, hash
// function and hash control), and afterwards ena_set_rxfh.
// Modelled (lx_models.c): the admin queue (the device answers anything, with
// any status), the DMA and devm allocators (can fail; frees are checked
// against a ledger), netif_set_real_num_{tx,rx}_queues.
//
// Starting state: probed, RSS initialised by the real ena_com_rss_init, the
// interface never brought up (ENA_FLAG_DEV_UP clear).
//
// Obligations, on a failed ethtool -L (rc != 0):
//   CH_ERR_COUNT    num_io_queues is what it was before the call
//   CH_ERR_RSS      the RSS indirection table is still allocated
// and with --pointer-check, the following ethtool -X does not dereference a
// released table.

#include "ena_netdev.h"
#include <linux/ethtool.h>

#define LX_LOG_TBL 3 // device-advertised indirection table: at most 8 entries

int __CPROVER_file_local_ena_ethtool_c_ena_set_channels(
  struct net_device *netdev,
  struct ethtool_channels *channels);
int __CPROVER_file_local_ena_ethtool_c_ena_set_rxfh(
  struct net_device *netdev,
  struct ethtool_rxfh_param *rxfh,
  struct netlink_ext_ack *extack);

// The admin model's answer to the indirection-table capability query is
// bounded to keep the per-entry loops short.  Every other field is free.
static void lx_bound_answer(
  struct ena_admin_aq_entry *cmd,
  struct ena_admin_acq_entry *comp)
{
  struct ena_admin_get_feat_cmd *g = (struct ena_admin_get_feat_cmd *)cmd;
  struct ena_admin_get_feat_resp *r = (struct ena_admin_get_feat_resp *)comp;
  if(
    cmd->aq_common_descriptor.opcode == ENA_ADMIN_GET_FEATURE &&
    g->feat_common.feature_id == ENA_ADMIN_RSS_INDIRECTION_TABLE_CONFIG)
    __CPROVER_assume(r->u.ind_table.max_size <= LX_LOG_TBL);
}
#define LX_ADMIN_HOOK(cmd, comp) lx_bound_answer(cmd, comp)

#include "lx_models.c"

int netif_set_real_num_tx_queues(struct net_device *dev, unsigned int txq)
{
  int rc = nondet_bool() ? 0 : -EINVAL;
  (void)txq;
  if(rc == 0)
    dev->real_num_tx_queues = txq;
  return rc;
}

int netif_set_real_num_rx_queues(struct net_device *dev, unsigned int rxq)
{
  int rc = nondet_bool() ? 0 : -EINVAL;
  (void)rxq;
  if(rc == 0)
    dev->real_num_rx_queues = rxq;
  return rc;
}

static struct lx_dev
{
  struct net_device nd;
  struct ena_adapter ad;
} lx_dev;
_Static_assert(
  __builtin_offsetof(struct lx_dev, ad) ==
    __builtin_offsetof(struct net_device, priv),
  "ena_adapter must sit where netdev_priv() points");
static struct ena_com_dev lx_ena_dev;
static struct pci_dev lx_pdev;

int main(void)
{
  struct net_device *netdev = &lx_dev.nd;
  struct ena_adapter *adapter = netdev_priv(netdev);
  struct ena_com_dev *ena_dev = &lx_ena_dev;
  struct ethtool_channels ch;
  struct ethtool_rxfh_param rxfh;
  u32 indir[1 << LX_LOG_TBL];
  u32 n0, i;
  int rc;

  __CPROVER_assert(adapter == &lx_dev.ad, "LX_LAYOUT: netdev_priv() is the adapter");
  adapter->netdev = netdev;
  adapter->ena_dev = ena_dev;
  adapter->pdev = &lx_pdev;
  ena_dev->net_device = netdev;
  ena_dev->dmadev = &lx_pdev.dev;
  ena_dev->admin_queue.running_state = true;
  ena_dev->supported_features = nondet_uint();
  adapter->xdp_bpf_prog = NULL;
  adapter->flags = 0;
  if(nondet_bool())
    set_bit(ENA_FLAG_DEVICE_RUNNING, &adapter->flags);

  adapter->max_num_io_queues = 1 + (nondet_uint() % 4);
  n0 = 1 + (nondet_uint() % adapter->max_num_io_queues);
  adapter->num_io_queues = n0;
  adapter->requested_tx_ring_size = ENA_MIN_RING_SIZE;
  adapter->requested_rx_ring_size = ENA_MIN_RING_SIZE;

  // probe initialised RSS
  rc = ena_com_rss_init(ena_dev);
  __CPROVER_assume(rc == 0);

  // ethtool -L
  ch.combined_count = nondet_uint();
  rc = __CPROVER_file_local_ena_ethtool_c_ena_set_channels(netdev, &ch);

  if(rc != 0)
  {
    __CPROVER_assert(
      adapter->num_io_queues == n0,
      "CH_ERR_COUNT: a failed ethtool -L leaves the queue count unchanged");
    __CPROVER_assert(
      ena_dev->rss.host_rss_ind_tbl != NULL,
      "CH_ERR_RSS: a failed ethtool -L leaves the RSS table allocated");
  }

#ifdef VACUITY_CHECK
  // Each must FAIL.
  __CPROVER_assert(rc == 0, "witness: ethtool -L can fail");
  __CPROVER_assert(
    !(rc != 0 && ch.combined_count != n0 &&
      ch.combined_count >= ENA_MIN_NUM_IO_QUEUES &&
      ch.combined_count <= adapter->max_num_io_queues),
    "witness: a valid new count can fail past the range checks");
  __CPROVER_assert(rc != 0, "witness: ethtool -L can succeed");
#endif

  // ethtool -X with a valid table, as userspace would pass it
  for(i = 0; i < (1 << LX_LOG_TBL); i++)
  {
    indir[i] = nondet_uint();
    __CPROVER_assume(indir[i] < adapter->num_io_queues);
  }
  rxfh.indir = indir;
  rxfh.key = NULL;
  rxfh.hfunc = ETH_RSS_HASH_NO_CHANGE;
  rxfh.indir_size = 1 << LX_LOG_TBL;
  rxfh.key_size = 0;
  rc = __CPROVER_file_local_ena_ethtool_c_ena_set_rxfh(netdev, &rxfh, NULL);
#ifdef VACUITY_CHECK
  __CPROVER_assert(0, "witness: ethtool -X is reached");
#endif
  return 0;
}
