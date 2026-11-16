// Class S on Linux ENA: does `ethtool -G` take effect when it returns 0?
//
// Unveil (Linux ENA, class Z, reclassified S): "ethtool -G silently ignored
// after a memory-pressure ring-size backoff".
//
// Real code: ena_set_ringparam and ena_get_ringparam (ena_ethtool.c), with
// clamp_val / rounddown_pow_of_two from the kernel headers.
//
// Modelled: ena_update_queue_params (ena_netdev.c:3064), which closes the
// device, records the request in requested_{tx,rx}_ring_size, re-initialises
// the rings from it (ena_init_io_rings, ena_netdev.c:341/356) and, if the
// device was up, brings it up through create_queues_with_size_backoff. The
// model keeps that contract: on rc == 0 the rings are the requested sizes,
// unless the backoff ran in this call, which is recorded in a ghost and
// excuses the obligation.
//
// The state before the call is the one the backoff leaves behind
// (create_queues_with_size_backoff, ena_netdev.c:2664-2756): it shrinks
// tx_ring[i].ring_size through set_io_rings_size but leaves
// requested_tx_ring_size alone.
//
//   PRIOR_BACKOFF=0  rings == requested before the call: SR_TAKES_EFFECT holds
//   PRIOR_BACKOFF=1  rings <  requested before the call: expected to FAIL
//
// Obligation SR_TAKES_EFFECT: if ena_set_ringparam returns 0 and no backoff
// ran during the call, ena_get_ringparam then reports exactly the sizes the
// request was normalised to (clamped to [ENA_MIN_RING_SIZE, max], rounded
// down to a power of two).

#include "ena_netdev.h"
#include <linux/ethtool.h>

#ifndef PRIOR_BACKOFF
#  define PRIOR_BACKOFF 0
#endif

// cap on the device maxima, to keep the power-of-two loops short
#define LX_MAX_RING 4096

int __CPROVER_file_local_ena_ethtool_c_ena_set_ringparam(
  struct net_device *netdev,
  struct ethtool_ringparam *ring,
  struct kernel_ethtool_ringparam *kernel_ring,
  struct netlink_ext_ack *extack);
void __CPROVER_file_local_ena_ethtool_c_ena_get_ringparam(
  struct net_device *netdev,
  struct ethtool_ringparam *ring,
  struct kernel_ethtool_ringparam *kernel_ring,
  struct netlink_ext_ack *extack);

unsigned int nondet_uint(void);
int nondet_int(void);
_Bool nondet_bool(void);

static int ghost_update_called;
static int ghost_backoff;

int ena_update_queue_params(
  struct ena_adapter *adapter,
  u32 new_tx_size,
  u32 new_rx_size,
  u32 new_llq_header_len)
{
  int rc = nondet_int();
  u32 i, tx = new_tx_size, rx = new_rx_size;

  (void)new_llq_header_len;
  ghost_update_called = 1;
  adapter->requested_tx_ring_size = new_tx_size;
  adapter->requested_rx_ring_size = new_rx_size;
  if(rc == 0 && nondet_bool())
  {
    // create_queues_with_size_backoff shrank at least one queue
    ghost_backoff = 1;
    tx = nondet_uint();
    rx = nondet_uint();
    __CPROVER_assume(tx >= ENA_MIN_RING_SIZE && tx <= new_tx_size);
    __CPROVER_assume(rx >= ENA_MIN_RING_SIZE && rx <= new_rx_size);
  }
  for(i = 0; i < adapter->num_io_queues; i++)
  {
    adapter->tx_ring[i].ring_size = tx;
    adapter->rx_ring[i].ring_size = rx;
  }
  return rc;
}

static u32 pow2(void)
{
  u32 k = nondet_uint();
  __CPROVER_assume(k <= 4); // 256 .. 4096
  return (u32)ENA_MIN_RING_SIZE << k;
}

// independent statement of the normalisation ethtool -G applies
static u32 normalise(u32 v, u32 max)
{
  u32 p = ENA_MIN_RING_SIZE;
  if(v < ENA_MIN_RING_SIZE)
    v = ENA_MIN_RING_SIZE;
  if(v > max)
    v = max;
  while(p * 2 <= v)
    p *= 2;
  return p;
}

// The net_device and its private area as one typed object.  netdev_priv()
// returns dev->priv, the flexible array at the end of struct net_device;
// with a malloc'ed byte buffer every adapter access became a byte extract
// from a several-hundred-KiB untyped object and the propositional
// reduction ran out of 60 GB.  Placing a struct ena_adapter at exactly
// that offset keeps the accesses field-sensitive.
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

int main(void)
{
  struct net_device *netdev = &lx_dev.nd;
  struct ena_adapter *adapter;
  struct ena_com_dev *ena_dev = &lx_ena_dev;
  struct ethtool_ringparam ring;
  struct kernel_ethtool_ringparam kring;
  struct ethtool_ringparam got;
  struct kernel_ethtool_ringparam kgot;
  u32 req_tx, req_rx, exp_tx, exp_rx;
  int rc;

  adapter = netdev_priv(netdev);
  __CPROVER_assert(adapter == &lx_dev.ad, "LX_LAYOUT: netdev_priv() is the adapter");
  adapter->netdev = netdev;
  adapter->ena_dev = ena_dev;
  ena_dev->tx_mem_queue_type =
    nondet_bool() ? ENA_ADMIN_PLACEMENT_POLICY_DEV
                  : ENA_ADMIN_PLACEMENT_POLICY_HOST;
  ena_dev->tx_max_header_size = nondet_bool() ? ENA_LLQ_HEADER
                                              : ENA_LLQ_LARGE_HEADER;
  adapter->num_io_queues = 1 + (nondet_uint() & 1);

  adapter->max_tx_ring_size = pow2();
  adapter->max_rx_ring_size = pow2();
  adapter->requested_tx_ring_size = pow2();
  adapter->requested_rx_ring_size = pow2();
  __CPROVER_assume(
    adapter->requested_tx_ring_size <= adapter->max_tx_ring_size &&
    adapter->requested_rx_ring_size <= adapter->max_rx_ring_size);

  // the rings as the last bring-up left them
  {
    u32 tx = adapter->requested_tx_ring_size;
    u32 rx = adapter->requested_rx_ring_size;
    u32 i;
#if PRIOR_BACKOFF
    tx = pow2();
    rx = pow2();
    __CPROVER_assume(
      tx <= adapter->requested_tx_ring_size &&
      rx <= adapter->requested_rx_ring_size &&
      (tx < adapter->requested_tx_ring_size ||
       rx < adapter->requested_rx_ring_size));
#endif
    for(i = 0; i < adapter->num_io_queues; i++)
    {
      adapter->tx_ring[i].ring_size = tx;
      adapter->rx_ring[i].ring_size = rx;
    }
  }

  // the request: anything userspace can pass
  req_tx = nondet_uint();
  req_rx = nondet_uint();
  ring.tx_pending = req_tx;
  ring.rx_pending = req_rx;
  ring.rx_mini_pending = nondet_bool() ? 0 : nondet_uint();
  ring.rx_jumbo_pending = nondet_bool() ? 0 : nondet_uint();
  kring.tx_push = nondet_bool();
  kring.tx_push_buf_len = nondet_bool() ? 0 : nondet_uint();

  rc = __CPROVER_file_local_ena_ethtool_c_ena_set_ringparam(
    netdev, &ring, &kring, NULL);

  __CPROVER_file_local_ena_ethtool_c_ena_get_ringparam(
    netdev, &got, &kgot, NULL);

  exp_tx = normalise(req_tx, adapter->max_tx_ring_size);
  exp_rx = normalise(req_rx, adapter->max_rx_ring_size);

  if(rc == 0 && !ghost_backoff)
  {
    __CPROVER_assert(
      got.tx_pending == exp_tx && got.rx_pending == exp_rx,
      "SR_TAKES_EFFECT: ethtool -G returning 0 means get reports the "
      "requested sizes");
  }

#ifdef VACUITY_CHECK
  // Each must FAIL.
  __CPROVER_assert(
    !(rc == 0 && !ghost_backoff),
    "witness: a successful ethtool -G without backoff is reachable");
  __CPROVER_assert(
    !(rc == 0 && !ghost_update_called),
    "witness: ethtool -G can return 0 without updating the queues");
  __CPROVER_assert(
    !(rc == 0 && ghost_update_called && !ghost_backoff),
    "witness: ethtool -G can return 0 after updating the queues");
#endif
  return 0;
}
