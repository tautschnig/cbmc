/*
 * F16: DPDK ena_rx_mbuf dereferences rx_buffer_info[req_id].mbuf for a
 * device-supplied req_id with only a compiled-out RTE_ASSERT, no NULL check.
 * ena_com_rx_pkt validates req_id < q_depth but not that the slot is posted.
 * Linux (ena_netdev.c ena_rx_skb) and FreeBSD (ena_datapath.c ena_rx_mbuf)
 * both check rx_info->{page,mbuf} == NULL and reset instead of dereferencing.
 *
 * This harness calls the real ena_rx_mbuf with one buffer descriptor whose
 * req_id names a slot the invariant allows to be free (mbuf == NULL), and
 * checks the dereference.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING_SIZE
#define RING_SIZE 4
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_adapter adapter;
static struct ena_ring ring;
static struct ena_rx_buffer pool_rbi[RING_SIZE];
static uint16_t pool_err[RING_SIZE];
static struct rte_mbuf pool_mbuf[RING_SIZE];
static struct ena_com_rx_buf_info ebufs[ENA_PKT_MAX_BUFS];

int rte_log(uint32_t l, uint32_t t, const char *f, ...) { (void)l;(void)t;(void)f; return 0; }
int rte_log_register_type_and_pick_level(const char *n, uint32_t l) { (void)n;(void)l; return 0; }
void rte_pci_register(struct rte_pci_driver *d) { (void)d; }

int main(void)
{
  unsigned i;
  ring.adapter = &adapter;
  ring.type = ENA_RING_TYPE_RX;
  ring.ring_size = RING_SIZE;
  ring.size_mask = RING_SIZE - 1;
  ring.rx_buffer_info = pool_rbi;
  ring.empty_rx_reqs = pool_err;
  ring.port_id = 0;
  ring.ena_com_io_sq = &adapter.ena_dev.io_sq_queues[ENA_IO_RXQ_IDX(0)];
  ring.ena_com_io_cq = &adapter.ena_dev.io_cq_queues[ENA_IO_RXQ_IDX(0)];
  ring.rx_refill_buffer = (struct rte_mbuf **)pool_err; /* non-NULL */
  ring.mb_pool = (struct rte_mempool *)&adapter; /* non-NULL */
  ring.configured = 1;
  ring.sgl_size = ENA_PKT_MAX_BUFS;
  adapter.ena_dev.io_sq_queues[ENA_IO_RXQ_IDX(0)].q_depth = RING_SIZE;
  ring.next_to_clean = nd_u16() & (RING_SIZE - 1);
  ring.next_to_use = nd_u16() & (RING_SIZE - 1);

  for(i = 0; i < RING_SIZE; i++)
  {
    pool_rbi[i].mbuf = nd_int() ? &pool_mbuf[i] : NULL;
    pool_err[i] = nd_u16();
    __CPROVER_assume(pool_err[i] < RING_SIZE);
  }
  __CPROVER_assume(INV_RX_STARTED(&ring, RING_SIZE));

  /* one single-buffer completion from the device: req_id in range (the EIO
   * check in ena_com_rx_pkt), length nonzero */
  uint16_t req_id = nd_u16();
  __CPROVER_assume(req_id < RING_SIZE);          /* ena_com_rx_pkt guarantees this */
  ebufs[0].req_id = req_id;
  ebufs[0].len = nd_u16();
  __CPROVER_assume(ebufs[0].len >= 1);

  uint16_t ntc = ring.next_to_clean;
  /* RX_REQID_POSTED: the driver dereferences this slot's mbuf; is it posted? */
  __CPROVER_assert(
    ring.rx_buffer_info[req_id].mbuf != NULL,
    "RX_REQID_POSTED: ena_rx_mbuf only dereferences posted slots");

#ifdef WITNESS
  /* must FAIL: a device req_id CAN name a free slot under INV_RX_STARTED */
  __CPROVER_assert(ring.rx_buffer_info[req_id].mbuf != NULL,
                   "witness: a free-slot req_id is reachable");
  return 0;
#endif
  struct rte_mbuf *m = ena_rx_mbuf(&ring, ebufs, 1, &ntc, 0);
  (void)m;
  return 0;
}
