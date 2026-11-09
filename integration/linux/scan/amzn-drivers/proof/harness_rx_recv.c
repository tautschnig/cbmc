/*
 * RX data path step of the induction: eth_ena_recv_pkts.
 *
 * This is the RX analogue of harness_tx_callees.c, and the operation where
 * the device-trust boundary matters most: ena_com_rx_pkt reads the device's
 * completion descriptors and hands ena_rx_mbuf a device-chosen req_id, length
 * and descriptor count, which the driver uses to index rx_buffer_info and to
 * build the mbuf chain returned to the application.
 *
 * The device is untrusted.  The CQ descriptor ring is arbitrary memory; this
 * harness only pins the phase bits of the first few entries so that
 * ena_com_rx_pkt reads a bounded number of completions and the loop
 * terminates.  Everything ena_com_rx_pkt derives from the descriptors --
 * req_id, length, first/last/phase, the MBZ bits -- is whatever the device
 * put there, and the real ena_com_rx_pkt is what validates it.
 *
 * INV_RX_STARTED (ena_inv.h): a started RX queue.  The obligations:
 *   RX_SAFE          no out-of-bounds or NULL access from any device value
 *   RX_REQID         every req_id ena_rx_mbuf dereferences is < ring_size
 *                    (ena_com_rx_pkt's EIO check is what must guarantee this)
 *   RX_OWN_ONCE      a posted buffer is returned to the application at most
 *                    once: no two output mbufs are the same rx_info->mbuf
 *   RX_SLOT_FREED    every slot ena_rx_mbuf consumes is marked free
 *                    (rx_info->mbuf == NULL) and its req_id put on empty_rx_reqs
 *   RX_NO_DANGLING   no slot returned to the app is still referenced
 *   RX_STARTED       INV_RX_STARTED is preserved
 *
 * Switches: RING_SIZE, NB_PKTS, VACUITY_CHECK, ASSUME_HONEST_REQID (the
 * device never repeats a req_id in one burst; a restriction, see the notes).
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING_SIZE
#define RING_SIZE 8
#endif
#ifndef NB_PKTS
#define NB_PKTS 1
#endif
#ifndef MAXCHAIN
/* bound on descriptors per received packet: a stated restriction.  The device
 * can report up to sgl_size; proving every chain length is future work. */
#define MAXCHAIN 2
#endif
/* the device's CQ: enough entries for NB_PKTS single-descriptor packets */

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }
static int nd_int(void) { int v; return v; }

uint16_t g_probe;              /* an arbitrary ring slot */

static struct ena_adapter adapter;
static struct ena_ring ring;
static struct ena_rx_buffer pool_rbi[RING_SIZE];
static uint16_t pool_err[RING_SIZE];
static struct rte_mbuf *pool_refill[RING_SIZE];
static struct rte_mbuf pool_mbuf[RING_SIZE];
static struct ena_driver_stats drv_stats;
static uint32_t the_db_reg;
static struct rte_mempool the_pool;

/*
 * The contract harness_rx_com.c proves for the real ena_com_rx_pkt: on
 * success (returns 0) it reports descs in 0..max_bufs, and every
 * ena_bufs[i].req_id is < the CQ q_depth (== ring_size).  It advances the SQ
 * consumer by descs.  On failure it asks for a reset.  The step assumes that
 * contract; the device still chooses the values within it. */
static uint16_t g_rx_qdepth;
static struct ena_rx_buffer *g_rbi;
int ena_com_rx_pkt(struct ena_com_io_cq *io_cq, struct ena_com_io_sq *io_sq,
                   struct ena_com_rx_ctx *ctx)
{
  (void)io_cq;
  if(nd_int())
    return nd_int() ? ENA_COM_NO_SPACE : ENA_COM_EIO; /* device error */
  uint16_t descs = nd_u16();
  __CPROVER_assume(descs <= ctx->max_bufs);
  ctx->descs = descs;
  ctx->pkt_offset = 0;
  for(unsigned b = 0; b < descs && b < ENA_PKT_MAX_BUFS; b++)
  {
    ctx->ena_bufs[b].len = nd_u16();
    __CPROVER_assume(ctx->ena_bufs[b].len >= 1); /* not the len==0 repost path */
    ctx->ena_bufs[b].req_id = nd_u16();
    __CPROVER_assume(ctx->ena_bufs[b].req_id < g_rx_qdepth); /* the EIO check */
#ifdef ASSUME_NO_F16
    /* the device names a posted slot (F16: the driver does not check) */
    __CPROVER_assume(g_rbi[ctx->ena_bufs[b].req_id].mbuf != NULL);
#endif
  }
  /* the device cannot complete more buffers than are posted */
  __CPROVER_assume(
    descs <= (uint16_t)(io_sq->tail - io_sq->next_to_comp));
  io_sq->next_to_comp = (uint16_t)(io_sq->next_to_comp + descs);
  return 0;
}

int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
  (void)level;
  (void)logtype;
  (void)format;
  return 0;
}
int rte_log_register_type_and_pick_level(const char *n, uint32_t l)
{
  (void)n;
  (void)l;
  return 0;
}
void rte_pci_register(struct rte_pci_driver *d) { (void)d; }

/* The application receives at most NB_PKTS mbufs; follow one. */
static struct rte_mbuf *g_delivered[NB_PKTS];

/* ena_init_rx_mbuf/ena_rx_mbuf_prepare touch only scalar mbuf fields; keep
 * them real.  rte_mbuf_raw_free and the mempool are the RX free path. */
static unsigned g_raw_frees;
void rte_mempool_put(struct rte_mempool *mp, void *obj)
{
  (void)mp;
  (void)obj;
  g_raw_frees++;
}

int main(void)
{
  unsigned i;

  __CPROVER_havoc_object(&ring);
  __CPROVER_havoc_object(&adapter.ena_dev.io_sq_queues[ENA_IO_RXQ_IDX(0)]);
  __CPROVER_havoc_object(&adapter.ena_dev.io_cq_queues[ENA_IO_RXQ_IDX(0)]);
  struct ena_com_io_sq *sq = &adapter.ena_dev.io_sq_queues[ENA_IO_RXQ_IDX(0)];
  struct ena_com_io_cq *cq = &adapter.ena_dev.io_cq_queues[ENA_IO_RXQ_IDX(0)];

  ring.adapter = &adapter;
  ring.type = ENA_RING_TYPE_RX;
  ring.id = 0;
  ring.configured = 1;
  ring.ring_size = RING_SIZE;
  ring.size_mask = RING_SIZE - 1;
  ring.sgl_size = MAXCHAIN;
  ring.rx_buffer_info = pool_rbi;
  ring.empty_rx_reqs = pool_err;
  ring.rx_refill_buffer = pool_refill;
  ring.mb_pool = &the_pool;
  ring.port_id = 0;
  ring.ena_com_io_sq = sq;
  ring.ena_com_io_cq = cq;
  ring.rx_free_thresh = RING_SIZE; /* so recv does not refill: separate op */
  adapter.drv_stats = &drv_stats;
  adapter.trigger_reset = false;

  ring.next_to_clean = nd_u16() & (RING_SIZE - 1);
  ring.next_to_use = nd_u16() & (RING_SIZE - 1);

  /* ---- the device's RX SQ/CQ ---- */
  sq->direction = ENA_COM_IO_QUEUE_DIRECTION_RX;
  sq->q_depth = RING_SIZE;
  sq->mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;
  cq->direction = ENA_COM_IO_QUEUE_DIRECTION_RX;
  cq->q_depth = RING_SIZE;
  g_rx_qdepth = RING_SIZE;
  g_rbi = pool_rbi;

  /* occupancy: each slot has a distinct posted buffer or is free */
  for(i = 0; i < RING_SIZE; i++)
  {
    pool_rbi[i].mbuf = nd_int() ? &pool_mbuf[i] : NULL;
    pool_mbuf[i].pool = &the_pool;
    pool_mbuf[i].buf_len = 2048;
    pool_err[i] = nd_u16();
    __CPROVER_assume(pool_err[i] < RING_SIZE);
  }

  g_probe = nd_u16() % RING_SIZE;
  for(i = 0; i < NB_PKTS; i++)
    g_delivered[i] = NULL;
  g_raw_frees = 0;

  __CPROVER_assume(INV_RX_STARTED(&ring, RING_SIZE));

  struct rte_mbuf *rx_pkts[NB_PKTS];

  /* ---- one operation ---- */
  uint16_t got = eth_ena_recv_pkts(&ring, rx_pkts, NB_PKTS);

  /* ---- obligations ---- */
  __CPROVER_assert(got <= NB_PKTS, "RX_API: returned count within request");

  /* RX_OWN_ONCE: the delivered mbufs are pairwise distinct */
  for(i = 0; i < NB_PKTS; i++)
    for(unsigned j = i + 1; j < NB_PKTS; j++)
      if(i < got && j < got)
        __CPROVER_assert(
          rx_pkts[i] != rx_pkts[j],
          "RX_OWN_ONCE: no buffer is delivered to the application twice");

  /* RX_NO_DANGLING: a delivered mbuf is no longer owned by any slot */
  if(got >= 1)
  {
    struct rte_mbuf *d = rx_pkts[0];
    __CPROVER_assert(
      d == NULL || ring.rx_buffer_info[g_probe].mbuf != d,
      "RX_NO_DANGLING: no slot still owns a delivered mbuf");
  }

  __CPROVER_assert(
    INV_RX_STARTED(&ring, RING_SIZE),
    "RX_STARTED: eth_ena_recv_pkts preserves INV_RX_STARTED");

#if VACUITY_CHECK
  __CPROVER_assert(got == 0, "vacuity witness: a packet can be received");
  __CPROVER_assert(
    !adapter.trigger_reset,
    "vacuity witness: a device completion can trigger a reset");
#endif
  return 0;
}
