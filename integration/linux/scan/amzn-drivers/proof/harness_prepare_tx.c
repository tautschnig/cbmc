/*
 * Discharge ASSUME_PREPARE_TX_OK: check the contract that harness_tx_callees.c
 * stubs ena_com_prepare_tx with, against the real ena_eth_com.c.
 *
 * The xmit step replaces ena_com_prepare_tx by a stub that, on success, writes
 * between 1 and num_bufs + 2 descriptors and keeps tail - next_to_comp below
 * q_depth; ASSUME_PREPARE_TX_OK additionally makes it always succeed.  This
 * harness proves, for ONE packet from an ARBITRARY between-packets SQ state
 * (INV_LLQ_PKT), that the real doorbell-check + prepare_tx sequence
 *
 *   PTX_OK        returns 0
 *   PTX_DESCS_MAX writes lines <= num_bufs + 2, lines = (u16)(tail - tail0)
 *   PTX_DESCS_MIN writes lines >= 1 for a non-empty packet (header_len >= 1
 *                 or num_bufs >= 1).  For an empty one -- a single-segment
 *                 mbuf with pkt_len == 0 -- and no meta descriptor it writes
 *                 nothing and returns 0: the "zero-length packet" witness.
 *   PTX_NB        reports nb_hw_desc == (int)tail - (int)tail0, the value the
 *                 real code computes from two u16s: equal to lines, except
 *                 that it is lines - 65536 (negative) when tail wraps.  Every
 *                 consumer reduces it mod 2^16 (ena_com_comp_ack takes a u16),
 *                 so only (u16)nb_hw_desc is meaningful.
 *   PTX_NO_OVERRUN keeps tail - next_to_comp <= q_depth - 1
 *   PTX_INV       re-establishes INV_LLQ_PKT (so the next packet starts from
 *                 a state this harness covers: induction over packets)
 *
 * plus the memory-safety checks and the device-copy bounds.  Inputs:
 *   - LLQ geometry: anything ena_com_config_llq_info accepts, with the
 *     device-init facts proven by harness_llq_path.c -DDEVINIT_ONLY
 *   - the packet: what ena_xmit_mbuf passes after its space check
 *     have_enough_space(nb_segs + 2), num_bufs <= nb_segs <= ENA_PKT_MAX_BUFS
 *   - -DASSUME_NO_F14: the device precondition (no TX-burst limit, or
 *     descs_num_before_header >= 2 and a budget that holds one maximal
 *     packet).  Without it PTX_OK fails (F14).
 *   - -DFIXED_ESTIMATE: the proposed fix instead of ASSUME_NO_F14's b >= 2.
 */
#include <stdbool.h>
#include <stdint.h>

#include "../drivers/net/ena/base/ena_plat_dpdk.h"

#ifndef QD
#define QD 64
#endif
#define BAR_BYTES (QD * 256)
static uint8_t bar[BAR_BYTES];

#undef ENA_MEMCPY_TO_DEVICE_64
#define ENA_MEMCPY_TO_DEVICE_64(dst, src, size)                                \
  __CPROVER_assert(                                                            \
    __CPROVER_r_ok((src), (size)) && __CPROVER_w_ok((dst), (size)) &&          \
      __CPROVER_same_object((dst), bar),                                       \
    "PTX_COPY: LLQ line copy stays inside the bounce buffer and the LLQ BAR")
#include "../drivers/net/ena/base/ena_eth_com.c"

#define ENA_COM_BOUNCE_BUFFER_CNTRL_CNT 4 /* as in ena_com.c */
#ifndef MAXB
#define MAXB 17 /* ENA_PKT_MAX_BUFS, ena_ethdev.h */
#endif

static struct ena_com_dev dev;
#define sq (dev.io_sq_queues[0])
static uint32_t db_reg;
static uint8_t bounce[ENA_COM_BOUNCE_BUFFER_CNTRL_CNT * 256];
static uint8_t push_src[256];
static struct ena_com_buf bufs[MAXB];

int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
  (void)level;
  (void)logtype;
  (void)format;
  return 0;
}

static uint8_t nd_u8(void) { uint8_t v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }
static bool nd_bool(void) { bool v; return v; }

/* the LLQ lines one packet of n data buffers needs, as the doorbell check
 * counts them (with the num_bufs == 0 case counted as one descriptor) */
static unsigned lines_for(unsigned n, bool meta)
{
  const struct ena_com_llq_info *li = &sq.llq_info;
  unsigned d = (n ? n : 1) + (meta ? 1 : 0);
  return d > li->descs_num_before_header
           ? 1 + DIV_ROUND_UP(d - li->descs_num_before_header, li->descs_per_entry)
           : 1;
}

/* INV_LLQ_PKT: the state between packets.  ena_com_init_io_sq leaves it, and
 * ena_com_close_bounce_buffer (the last thing prepare_tx does) re-creates it */
static bool INV_LLQ_PKT(void)
{
  const struct ena_com_llq_info *li = &sq.llq_info;
  return sq.llq_buf_ctrl.idx == 0 &&
         sq.llq_buf_ctrl.descs_left_in_line == li->descs_num_before_header &&
         sq.bounce_buf_ctrl.base_buffer == bounce &&
         sq.bounce_buf_ctrl.buffer_size == li->desc_list_entry_size &&
         sq.bounce_buf_ctrl.buffers_num == ENA_COM_BOUNCE_BUFFER_CNTRL_CNT &&
         sq.llq_buf_ctrl.curr_bounce_buf ==
           bounce + ((uint16_t)(sq.bounce_buf_ctrl.next_to_use - 1) &
                     (ENA_COM_BOUNCE_BUFFER_CNTRL_CNT - 1)) *
                      li->desc_list_entry_size &&
         sq.entries_in_tx_burst_left <= li->max_entries_in_tx_burst &&
         (uint16_t)(sq.tail - sq.next_to_comp) <= sq.q_depth - 1;
}

int main(void)
{
  struct ena_com_llq_info *li = &sq.llq_info;

  /* ---- LLQ geometry: what ena_com_config_llq_info can produce ---- */
  li->desc_list_entry_size = nd_bool() ? 256 : 128;
  li->desc_stride_ctrl = ENA_ADMIN_MULTIPLE_DESCS_PER_ENTRY;
  sq.desc_entry_size = sizeof(struct ena_eth_io_tx_desc);
  li->descs_per_entry = li->desc_list_entry_size / sq.desc_entry_size;
  const uint8_t b = nd_u8();
  __CPROVER_assume(b == 1 || b == 2 || b == 4 || b == 8);
  li->descs_num_before_header = b;
  /* config_dev_mode rejects tx_max_header_size == 0 */
  __CPROVER_assume(b * sq.desc_entry_size < li->desc_list_entry_size);
  li->disable_meta_caching = nd_bool();
  li->max_entries_in_tx_burst = nd_u16();
#ifdef ASSUME_NO_F14
  __CPROVER_assume(
    li->max_entries_in_tx_burst == 0 ||
    (b >= 2 && li->max_entries_in_tx_burst >= lines_for(MAXB, true)));
#endif
#ifdef FIXED_ESTIMATE
  __CPROVER_assume(
    li->max_entries_in_tx_burst == 0 ||
    li->max_entries_in_tx_burst >= lines_for(MAXB, true));
#endif

  /* ---- the SQ: fixed fields as ena_com_create_io_sq sets them ---- */
  sq.qid = 0;
  sq.mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  sq.q_depth = QD;
  sq.dma_addr_bits = 32 + nd_u8() % 17; /* device-init range (F13 aside) */
  sq.tx_max_header_size = li->desc_list_entry_size - b * sq.desc_entry_size;
  sq.bus = NULL;
  sq.db_addr = &db_reg;
  sq.desc_addr.pbuf_dev_addr = bar;
  sq.disable_meta_caching = li->disable_meta_caching;
  sq.bounce_buf_ctrl.base_buffer = bounce;
  sq.bounce_buf_ctrl.buffer_size = li->desc_list_entry_size;
  sq.bounce_buf_ctrl.buffers_num = ENA_COM_BOUNCE_BUFFER_CNTRL_CNT;

  /* ---- arbitrary between-packets state ---- */
  sq.bounce_buf_ctrl.next_to_use = nd_u16();
  sq.llq_buf_ctrl.curr_bounce_buf =
    bounce + ((uint16_t)(sq.bounce_buf_ctrl.next_to_use - 1) &
              (ENA_COM_BOUNCE_BUFFER_CNTRL_CNT - 1)) *
               li->desc_list_entry_size;
  sq.llq_buf_ctrl.idx = 0;
  sq.llq_buf_ctrl.descs_left_in_line = b;
  sq.entries_in_tx_burst_left = nd_u16();
  sq.tail = nd_u16();
  sq.next_to_comp = nd_u16();
  sq.phase = nd_u8() & 1;
  { /* arbitrary cached meta; not havoc_object, which would havoc all of dev */
    struct ena_com_tx_meta m;
    sq.cached_tx_meta = m;
  }
  __CPROVER_assume(INV_LLQ_PKT());

  /* ---- the packet ena_xmit_mbuf hands over ---- */
  struct ena_com_tx_ctx ctx; /* uninitialised local: nondeterministic */
  const uint16_t nb_segs = nd_u16();
  __CPROVER_assume(nb_segs >= 1 && nb_segs <= MAXB);
  __CPROVER_assume(ena_com_sq_have_enough_space(&sq, nb_segs + 2));
  __CPROVER_assume(ctx.num_bufs <= nb_segs);
  __CPROVER_assume(ctx.header_len <= sq.tx_max_header_size);
  ctx.push_header = push_src;
  ctx.ena_bufs = bufs;
  __CPROVER_assume(
    ctx.l3_proto == ENA_ETH_IO_L3_PROTO_UNKNOWN ||
    ctx.l3_proto == ENA_ETH_IO_L3_PROTO_IPV4 ||
    ctx.l3_proto == ENA_ETH_IO_L3_PROTO_IPV6);
  __CPROVER_assume(
    ctx.l4_proto == ENA_ETH_IO_L4_PROTO_UNKNOWN ||
    ctx.l4_proto == ENA_ETH_IO_L4_PROTO_TCP ||
    ctx.l4_proto == ENA_ETH_IO_L4_PROTO_UDP);

  const uint16_t tail0 = sq.tail;
  int nb_hw_desc = -1;

  /* ---- ena_xmit_mbuf's protocol ---- */
#ifdef FIXED_ESTIMATE
  bool need = false;
  if(is_llq_max_tx_burst_exists(&sq))
    need = lines_for(
             ctx.num_bufs,
             li->disable_meta_caching || ena_com_meta_desc_changed(&sq, &ctx)) >
           sq.entries_in_tx_burst_left;
#else
  bool need = ena_com_is_doorbell_needed(&sq, &ctx);
#endif
  if(need)
    ena_com_write_sq_doorbell(&sq);
  int rc = ena_com_prepare_tx(&sq, &ctx, &nb_hw_desc);

  /* ---- the contract ---- */
  __CPROVER_assert(rc == 0, "PTX_OK: ena_com_prepare_tx succeeds");
  if(rc == 0)
  {
    const uint16_t lines = (uint16_t)(sq.tail - tail0);
    __CPROVER_assert(
      lines <= ctx.num_bufs + 2, "PTX_DESCS_MAX: lines written <= num_bufs + 2");
    __CPROVER_assert(
      (ctx.header_len == 0 && ctx.num_bufs == 0) || lines >= 1,
      "PTX_DESCS_MIN: a non-empty packet writes at least one line");
    __CPROVER_assert(
      nb_hw_desc == (int)sq.tail - (int)tail0 && (uint16_t)nb_hw_desc == lines,
      "PTX_NB: nb_hw_desc == (int)tail - (int)tail0, == lines mod 2^16");
#ifdef CLAIM_NB_POSITIVE
    __CPROVER_assert(nb_hw_desc >= 1, "PTX_NB_POSITIVE: nb_hw_desc >= 1 (false: wraps)");
#endif
    __CPROVER_assert(
      (uint16_t)(sq.tail - sq.next_to_comp) <= sq.q_depth - 1,
      "PTX_NO_OVERRUN: the producer stays within the queue");
    __CPROVER_assert(INV_LLQ_PKT(), "PTX_INV: the between-packets state is re-established");
  }

#ifdef VACUITY_CHECK
  __CPROVER_assert(rc != 0 || (uint16_t)(sq.tail - tail0) < 2, "vacuity witness: a two-line packet is sent");
  /* not a vacuity witness: FAILURE here is the finding that an empty packet
   * is accepted with no descriptor written */
  __CPROVER_assert(
    rc != 0 || (uint16_t)(sq.tail - tail0) != 0,
    "zero-length witness: a packet is accepted with zero lines written");
  __CPROVER_assert(rc != 0 || nb_hw_desc > 0, "vacuity witness: a packet wraps the tail");
  __CPROVER_assert(
    rc != 0 || !need, "vacuity witness: a packet is sent after a burst doorbell");
  __CPROVER_assert(
    rc != 0 || ctx.num_bufs < MAXB, "vacuity witness: a maximal packet is sent");
#endif
  return 0;
}
