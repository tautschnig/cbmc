/*
 * Discharge the ena_com_rx_pkt contract that harness_rx_recv.c stubs, against
 * the real ena_eth_com.c.
 *
 * The recv step replaces ena_com_rx_pkt by a stub that, on success, reports
 * descs <= max_bufs with every ena_bufs[i].req_id < q_depth, advances the SQ
 * consumer by descs, and completes no more than was posted.  This harness
 * proves that for one call on the real code, with the completion-descriptor
 * ring being arbitrary device memory.
 *
 * Only ena_eth_com.c is compiled in, so struct ena_ring's anonymous unions --
 * which make --pointer-check report ERROR on byte_extract bounds inside the
 * driver -- are not involved, the same reason harness_prepare_tx.c works.
 *
 *   RXC_DESCS      on success descs <= max_bufs
 *   RXC_REQID      on success every reported req_id < q_depth
 *   RXC_CONSUMER   the SQ consumer advances by exactly descs
 *   RXC_BUFS       only the first descs entries of ena_bufs are written
 *   plus --bounds-check on every CQ descriptor read
 *
 * Switches: QD (queue depth), MAXB (ena_bufs capacity / max_bufs cap),
 * VACUITY_CHECK.
 */
#include <stdbool.h>
#include <stdint.h>

#include "../drivers/net/ena/base/ena_plat_dpdk.h"
#include "../drivers/net/ena/base/ena_eth_com.c"

#ifndef QD
#define QD 4
#endif
#ifndef MAXB
#define MAXB 2
#endif
#define CDESC_SZ ((unsigned)sizeof(struct ena_eth_io_rx_cdesc_base))

static uint8_t nd_u8(void) { uint8_t v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static bool nd_bool(void) { bool v; return v; }

static struct ena_com_dev dev;
#define cq (dev.io_cq_queues[0])
#define sq (dev.io_sq_queues[0])

/* the device's completion-descriptor ring */
static struct ena_eth_io_rx_cdesc_base cq_ring[QD];
/* a canary entry after the ring: if the real code reads or writes past the
 * ring this harness would not otherwise notice, since cq_ring is an array */
static struct ena_com_rx_buf_info bufs[MAXB + 1];

/* ena_logtype_com is defined by RTE_LOG_REGISTER in ena_ethdev.c, which this
 * harness does not compile in; leaving the extern uninitialised makes it
 * nondeterministic and the (uint32_t) conversion in the trace macros report a
 * spurious conversion failure. */
int ena_logtype_com = 0;

int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
  (void)level;
  (void)logtype;
  (void)format;
  return 0;
}

int main(void)
{
  unsigned i;

  /* ---- the CQ as ena_com_create_io_cq leaves it ---- */
  cq.qid = 0;
  cq.direction = ENA_COM_IO_QUEUE_DIRECTION_RX;
  cq.q_depth = QD;
  cq.cdesc_entry_size_in_bytes = CDESC_SZ;
  cq.cdesc_addr.virt_addr = (u8 *)cq_ring;
  cq.head = nd_u16();
  cq.phase = nd_u8() & 1;
  /* a packet may already be partly consumed: cur_rx_pkt_cdesc_count counts
   * descriptors of the current packet seen so far */
  cq.cur_rx_pkt_cdesc_count = nd_u16();
  __CPROVER_assume(cq.cur_rx_pkt_cdesc_count <= QD);
  cq.cur_rx_pkt_cdesc_start_idx = nd_u16();
  __CPROVER_assume(cq.cur_rx_pkt_cdesc_start_idx < QD);

  sq.qid = 0;
  sq.direction = ENA_COM_IO_QUEUE_DIRECTION_RX;
  sq.q_depth = QD;
  sq.tail = nd_u16();
  sq.next_to_comp = nd_u16();

  /* the device's capability bits, including CDESC_MBZ */
  dev.capabilities = nd_u32();

  /* the ring is arbitrary device memory */
  for(i = 0; i < QD; i++)
  {
    cq_ring[i].status = nd_u32();
    cq_ring[i].length = nd_u16();
    cq_ring[i].req_id = nd_u16();
    cq_ring[i].hash = nd_u32();
    cq_ring[i].sub_qid = nd_u16();
    cq_ring[i].offset = nd_u8();
    cq_ring[i].reserved = nd_u8();
  }

  struct ena_com_rx_ctx ctx; /* uninitialised: nondeterministic */
  ctx.ena_bufs = bufs;
  ctx.max_bufs = nd_u16();
  __CPROVER_assume(ctx.max_bufs >= 1 && ctx.max_bufs <= MAXB);
  ctx.descs = 0;
  ctx.pkt_offset = 0;

  /* the canary: ena_bufs[max_bufs] must not be written */
  const uint16_t canary_req = nd_u16();
  const uint16_t canary_len = nd_u16();
  bufs[MAXB].req_id = canary_req;
  bufs[MAXB].len = canary_len;

  const uint16_t comp0 = sq.next_to_comp;

  /* ---- the call ---- */
  int rc = ena_com_rx_pkt(&cq, &sq, &ctx);

  /* ---- the contract the recv step assumes ---- */
  if(rc == 0)
  {
    __CPROVER_assert(
      ctx.descs <= ctx.max_bufs, "RXC_DESCS: descs <= max_bufs on success");
    for(i = 0; i < MAXB; i++)
      if(i < ctx.descs)
        __CPROVER_assert(
          bufs[i].req_id < sq.q_depth,
          "RXC_REQID: every reported req_id is below the queue depth");
    __CPROVER_assert(
      sq.next_to_comp == (uint16_t)(comp0 + ctx.descs),
      "RXC_CONSUMER: the SQ consumer advances by exactly descs");
    __CPROVER_assert(
      bufs[MAXB].req_id == canary_req && bufs[MAXB].len == canary_len,
      "RXC_BUFS: nothing is written past ena_bufs[max_bufs - 1]");
  }
#ifdef VACUITY_CHECK
  __CPROVER_assert(rc != 0, "vacuity witness: a completion can be reported");
  __CPROVER_assert(
    rc != 0 || ctx.descs == 0,
    "vacuity witness: a non-empty packet can be reported");
  __CPROVER_assert(
    rc != ENA_COM_EIO, "vacuity witness: a bad req_id is rejected with EIO");
  __CPROVER_assert(
    rc != ENA_COM_NO_SPACE,
    "vacuity witness: too many descriptors is rejected with NO_SPACE");
  __CPROVER_assert(
    rc != ENA_COM_FAULT, "vacuity witness: a malformed cdesc is rejected");
#endif
  return 0;
}
