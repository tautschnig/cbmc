/*
 * F14: the LLQ TX-burst budget check under-counts a fully pushed packet.
 *
 * The driver protocol (eth_ena_xmit_pkts -> ena_xmit_mbuf, and the same in
 * Linux ena_start_xmit and FreeBSD ena_xmit_mbuf) is, per packet:
 *
 *     if (ena_com_is_doorbell_needed(sq, ctx)) ena_com_write_sq_doorbell(sq);
 *     rc = ena_com_prepare_tx(sq, ctx, &n);
 *
 * The doorbell resets entries_in_tx_burst_left, and prepare_tx spends one
 * unit of it per LLQ line written (ena_com_write_bounce_buffer_to_dev), so
 * the check is sound only if it predicts the lines prepare_tx will write.
 *
 * This harness runs that protocol on the real ena_eth_com.c over a sequence
 * of NPKT packets from the SQ state ena_com_init_io_sq produces, with every
 * device-chosen LLQ parameter nondeterministic within what device init
 * accepts, and asserts prepare_tx never fails with ENA_COM_NO_SPACE.
 *
 *   -DBUDGET_FITS_PACKET  the device budget covers one maximal packet
 *   -DONLY_B_GE_2 / -DONLY_META_CACHING / -DONLY_NUM_BUFS_POSITIVE
 *                     remove one ingredient of F14 at a time
 *   -DWITH_OFFLOADS   meta_valid nondeterministic (offloads requested)
 *   -DFIXED_ESTIMATE  replace the doorbell predicate with one that counts the
 *                     descriptor prepare_tx writes even when num_bufs == 0
 *   -DVACUITY_CHECK   witnesses (must FAIL)
 */
#include <stdbool.h>
#include <stdint.h>

#include "../drivers/net/ena/base/ena_plat_dpdk.h"
#undef ENA_MEMCPY_TO_DEVICE_64
#define ENA_MEMCPY_TO_DEVICE_64(dst, src, size)                                \
  do                                                                           \
  {                                                                            \
    (void)(dst);                                                               \
    (void)(src);                                                               \
    (void)(size);                                                              \
  } while(0)
#include "../drivers/net/ena/base/ena_eth_com.c"

#ifndef NPKT
#define NPKT 3
#endif
#define QDEPTH 64
#define MAXBUFS 3
#define ENA_COM_BOUNCE_BUFFER_CNTRL_CNT 4 /* as in ena_com.c */

static uint8_t nd_u8(void) { uint8_t v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }
static bool nd_bool(void) { bool v; return v; }

static struct ena_com_dev dev;
#define sq (dev.io_sq_queues[0])
static uint32_t db_reg;
static uint8_t bounce[ENA_COM_BOUNCE_BUFFER_CNTRL_CNT * 256];
static uint8_t push_src[256];
static struct ena_com_buf bufs[MAXBUFS];
static unsigned lines_written;

int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
  (void)level;
  (void)logtype;
  (void)format;
  return 0;
}

#ifdef FIXED_ESTIMATE
/* Same as ena_com_is_doorbell_needed, except that a packet always has at
 * least one data descriptor: prepare_tx writes one even for num_bufs == 0. */
static bool fixed_doorbell_needed(
  struct ena_com_io_sq *io_sq,
  struct ena_com_tx_ctx *ctx)
{
  struct ena_com_llq_info *llq_info = &io_sq->llq_info;
  int num_entries_needed = 1;
  u16 num_descs;

  if(!is_llq_max_tx_burst_exists(io_sq))
    return false;
  num_descs = ctx->num_bufs ? ctx->num_bufs : 1;
  if(llq_info->disable_meta_caching || ena_com_meta_desc_changed(io_sq, ctx))
    ++num_descs;
  if(num_descs > llq_info->descs_num_before_header)
    num_entries_needed += DIV_ROUND_UP(
      num_descs - llq_info->descs_num_before_header, llq_info->descs_per_entry);
  return num_entries_needed > io_sq->entries_in_tx_burst_left;
}
#define DOORBELL_NEEDED fixed_doorbell_needed
#else
#define DOORBELL_NEEDED ena_com_is_doorbell_needed
#endif

int main(void)
{
  struct ena_com_llq_info *li = &sq.llq_info;

  /* ---- device-chosen LLQ geometry, as ena_com_config_llq_info accepts it */
  const bool large = nd_bool();
  li->desc_list_entry_size = large ? 256 : 128;
  li->desc_stride_ctrl = ENA_ADMIN_MULTIPLE_DESCS_PER_ENTRY;
  sq.desc_entry_size = sizeof(struct ena_eth_io_tx_desc);
  li->descs_per_entry = li->desc_list_entry_size / sq.desc_entry_size;
  const uint8_t b = nd_u8();
  __CPROVER_assume(b == 1 || b == 2 || b == 4 || b == 8);
  li->descs_num_before_header = b;
  __CPROVER_assume(b * sq.desc_entry_size < li->desc_list_entry_size);
  li->disable_meta_caching = nd_bool();
#ifdef ONLY_B_GE_2
  __CPROVER_assume(b >= 2);
#endif
#ifdef ONLY_META_CACHING
  __CPROVER_assume(!li->disable_meta_caching);
#endif
  li->max_entries_in_tx_burst = nd_u16();
  __CPROVER_assume(li->max_entries_in_tx_burst >= 1);
  __CPROVER_assume(li->max_entries_in_tx_burst <= 64);
#ifdef BUDGET_FITS_PACKET
  /* The device's burst budget is at least the number of LLQ lines one
   * maximal packet needs (MAXBUFS data descriptors + a meta descriptor), so
   * a doorbell before the packet always makes room.  Without this a single
   * packet can never be sent, whatever the driver does. */
  __CPROVER_assume(
    li->max_entries_in_tx_burst >=
    1 + DIV_ROUND_UP(MAXBUFS + 1, li->descs_per_entry));
#endif

  /* ---- the SQ as ena_com_create_io_sq + ena_com_init_io_sq leave it */
  sq.qid = 0;
  sq.dma_addr_bits = 48; /* F13 aside: a device-init result in 32..48 */
  sq.mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  sq.q_depth = QDEPTH;
  sq.tx_max_header_size =
    li->desc_list_entry_size - b * sq.desc_entry_size; /* config_dev_mode */
  sq.bus = NULL;
  sq.db_addr = &db_reg;
  sq.bounce_buf_ctrl.base_buffer = bounce;
  sq.bounce_buf_ctrl.buffer_size = li->desc_list_entry_size;
  sq.bounce_buf_ctrl.buffers_num = ENA_COM_BOUNCE_BUFFER_CNTRL_CNT;
  sq.bounce_buf_ctrl.next_to_use = 0;
  sq.llq_buf_ctrl.curr_bounce_buf =
    ena_com_get_next_bounce_buffer(&sq.bounce_buf_ctrl);
  sq.llq_buf_ctrl.idx = 0;
  sq.llq_buf_ctrl.descs_left_in_line = b;
  sq.disable_meta_caching = li->disable_meta_caching;
  sq.entries_in_tx_burst_left = li->max_entries_in_tx_burst;
  sq.tail = 0;
  sq.next_to_comp = 0;
  sq.phase = 1;

  unsigned sent = 0;
  int nb_hw_desc;
  for(unsigned p = 0; p < NPKT; p++)
  {
    struct ena_com_tx_ctx ctx;
    __CPROVER_havoc_object(&ctx);

    /* a packet ena_tx_map_mbuf can build in LLQ mode: the header is pushed
     * (push_len = min(pkt_len, tx_max_header_size)), and num_bufs counts the
     * remaining DMA buffers -- 0 when the whole packet fits in the push */
    ctx.push_header = push_src;
    __CPROVER_assume(ctx.header_len >= 1);
    __CPROVER_assume(ctx.header_len <= sq.tx_max_header_size);
    __CPROVER_assume(ctx.num_bufs <= MAXBUFS);
    __CPROVER_assume(ctx.num_bufs == 0 || ctx.header_len == sq.tx_max_header_size);
    ctx.ena_bufs = bufs;
#ifdef WITH_OFFLOADS
    /* offloads: ena_tx_mbuf_prepare sets meta_valid and the meta fields */
    ctx.meta_valid = nd_bool();
#else
    ctx.meta_valid = 0; /* no offloads, as in the induction */
#endif
    /* ena_tx_mbuf_prepare only stores enumerators */
    __CPROVER_assume(
      ctx.l3_proto == ENA_ETH_IO_L3_PROTO_UNKNOWN ||
      ctx.l3_proto == ENA_ETH_IO_L3_PROTO_IPV4 ||
      ctx.l3_proto == ENA_ETH_IO_L3_PROTO_IPV6);
    __CPROVER_assume(
      ctx.l4_proto == ENA_ETH_IO_L4_PROTO_UNKNOWN ||
      ctx.l4_proto == ENA_ETH_IO_L4_PROTO_TCP ||
      ctx.l4_proto == ENA_ETH_IO_L4_PROTO_UDP);
#ifdef ONLY_NUM_BUFS_POSITIVE
    __CPROVER_assume(ctx.num_bufs >= 1);
#endif

    /* the device has completed everything: no ENA_COM_NO_MEM */
    sq.next_to_comp = sq.tail;

    if(DOORBELL_NEEDED(&sq, &ctx))
      ena_com_write_sq_doorbell(&sq);
    const u16 budget_before = sq.entries_in_tx_burst_left;
    int rc = ena_com_prepare_tx(&sq, &ctx, &nb_hw_desc);
    (void)budget_before;

    __CPROVER_assert(
      rc != ENA_COM_NO_SPACE,
      "F14: the doorbell check leaves enough TX-burst budget for the packet");
    if(rc != 0)
      break;
    sent++;
  }

#ifdef VACUITY_CHECK
  __CPROVER_assert(sent < NPKT, "vacuity witness: all packets can be sent");
  __CPROVER_assert(
    li->max_entries_in_tx_burst < 8 || sent < NPKT,
    "vacuity witness: a large budget can carry the whole sequence");
#endif
  return 0;
}
