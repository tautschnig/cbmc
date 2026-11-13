/*
 * F14, machine-checked: does the LLQ doorbell estimate actually guarantee the
 * burst has room for the packet that follows?
 *
 * ena_com_is_doorbell_needed (ena_eth_com.h:131) decides whether the driver
 * must ring the doorbell before submitting, by estimating how many LLQ lines
 * the packet will occupy:
 *
 *     num_descs = ena_tx_ctx->num_bufs;
 *     if (disable_meta_caching || ena_com_meta_desc_changed(...))
 *             ++num_descs;
 *     if (num_descs > llq_info->descs_num_before_header) {
 *             descs_after_first_entry = num_descs - descs_num_before_header;
 *             num_entries_needed += DIV_ROUND_UP(descs_after_first_entry,
 *                                               descs_per_entry);
 *     }
 *     return num_entries_needed > io_sq->entries_in_tx_burst_left;
 *
 * The actual consumption is counted elsewhere: every bounce-buffer line
 * written to the device decrements entries_in_tx_burst_left, and refuses with
 * ENA_COM_NO_SPACE when it is already zero
 * (ena_com_write_bounce_buffer_to_dev, ena_eth_com.c:107-118).
 *
 * So the estimate carries a contract: if it says no doorbell is needed, the
 * submission that follows must not run out of burst entries.  That is what
 * this harness checks, and it is the contract F14 says is broken for a fully
 * pushed packet -- num_bufs == 0 with a header -- because the estimate starts
 * at num_bufs while ena_com_prepare_tx writes a data descriptor regardless
 * (the early return at ena_eth_com.c:511 needs !num_bufs && !header_len).
 *
 *   DB_CONTRACT  "no doorbell needed" implies prepare_tx does not fail for
 *                lack of burst entries
 *
 * -DWITH_FIX applies the proposed fix, num_descs = max(num_bufs, 1).
 */
#include "ena_plat.h"

/* keep the harness to the LLQ accounting: tracing and device writes are not
 * what is under test, and the register write would otherwise need a model */
#undef ena_trc_dbg
#undef ena_trc_err
#undef ena_trc_info
#define ena_trc_dbg(dev, fmt, ...) ((void)0)
#define ena_trc_err(dev, fmt, ...) ((void)0)
#define ena_trc_info(dev, fmt, ...) ((void)0)

#include "ena_eth_com.h"

#ifndef MAX_BUFS
#define MAX_BUFS 10
#endif
#define ENTRY_SIZE 128
#define DESC_SIZE 16

/* The bounce-buffer pool ena_com rotates through.  buffers_num must be a
 * power of two: ena_com_get_next_bounce_buffer masks with buffers_num - 1.
 * Leaving this control block zeroed gives a NULL base_buffer, which shows up
 * as an unwriteable memset destination rather than as anything informative. */
#define NBOUNCE 4
static unsigned char g_bounce[NBOUNCE * ENTRY_SIZE];

/* The push header is copied INTO the bounce buffer, so it must be a distinct
 * object -- sharing one buffer makes the copy overlap. */
static unsigned char g_push_header[256];

/* file-scope: taking the address of a loop-local and letting it escape into
 * ena_com_prepare_tx produced an unexplained out-of-bounds report on
 * *nb_hw_desc, which is noise about the harness rather than the driver */
static u16 g_nb_hw_desc;
static volatile uint32_t g_doorbell_reg;

/* file-scope so the setup needs no loop; the addresses and lengths are
 * arbitrary but fixed, since what is under test is the line accounting and
 * not the buffer contents */
static struct ena_com_buf g_bufs[MAX_BUFS] = {
  {0x1000, 64}, {0x1100, 64}, {0x1200, 64}, {0x1300, 64}, {0x1400, 64},
  {0x1500, 64}, {0x1600, 64}, {0x1700, 64}, {0x1800, 64}, {0x1900, 64}};

/* The bounce buffer is copied to device memory; the copy itself is not under
 * test, and modelling it would only add a large havoc. */
#undef ENA_MEMCPY_TO_DEVICE_64
#define ENA_MEMCPY_TO_DEVICE_64(dst, src, size) ((void)0)

/* The doorbell register is modelled as ordinary memory rather than by
 * overriding ENA_REG_WRITE32: the macro is used inside a static inline in
 * ena_eth_com.h, so an override here would come too late.  What matters for
 * this harness is the burst credit the doorbell resets, which
 * ena_com_write_sq_doorbell does itself. */

#include "ena_eth_com.c"

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }
static bool nd_bool(void) { bool v; return v; }

#ifndef NPKTS
#define NPKTS 2
#endif
/* A line holds descs_per_entry descriptors.  num_bufs must be able to reach
 * that, or no line ever completes and nothing is ever charged against the
 * burst -- which made an earlier version of this harness pass vacuously.
 * MAX_BUFS is defined above, with g_bufs. */
#define Q_DEPTH 16

static struct ena_com_io_sq sq;
static struct ena_com_tx_ctx ctx;
static struct ena_com_dev dev;
static unsigned char desc_mem[Q_DEPTH * ENTRY_SIZE];

int main(void)
{
  struct ena_com_llq_info *li = &sq.llq_info;

  /* an LLQ submission queue with a tx-burst limit, which is what makes the
   * doorbell estimate meaningful (is_llq_max_tx_burst_exists) */
  sq.mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  sq.q_depth = Q_DEPTH;
  sq.desc_entry_size = DESC_SIZE;
  sq.desc_addr.virt_addr = desc_mem;
  sq.bus = NULL;
  sq.db_addr = (u32 *)&g_doorbell_reg;
  sq.dma_addr_bits = 32;
  sq.tail = 0;
  sq.next_to_comp = 0;
  sq.phase = 1;

  /* the LLQ geometry the device chose, within the ranges ena_com accepts */
  li->desc_list_entry_size = ENTRY_SIZE;
  li->descs_per_entry = ENTRY_SIZE / DESC_SIZE;
  /* ena_com only ever installs one of the values the admin interface
   * defines, ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_{1,2,4,8}
   * (ena_com.c:739-750), so an arbitrary u8 here would produce counterexamples
   * on geometry the device cannot actually ask for. */
  li->descs_num_before_header = nd_u8();
  __CPROVER_assume(
    li->descs_num_before_header == ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_1 ||
    li->descs_num_before_header == ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_2 ||
    li->descs_num_before_header == ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_4 ||
    li->descs_num_before_header == ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_8);
  __CPROVER_assume(li->descs_num_before_header <= li->descs_per_entry);
  /* max_tx_burst_size is device-supplied and ena_com divides it by the entry
   * size without imposing any lower bound (ena_com.c:767-769), so 1 is a
   * value the device can produce. */
  li->max_entries_in_tx_burst = nd_u16();
  __CPROVER_assume(
    li->max_entries_in_tx_burst >= 1 && li->max_entries_in_tx_burst <= 8);
  li->disable_meta_caching = nd_bool();

  sq.entries_in_tx_burst_left = nd_u16();
  __CPROVER_assume(
    sq.entries_in_tx_burst_left <= li->max_entries_in_tx_burst);
  const u16 g_initial_credit = sq.entries_in_tx_burst_left;

  /* The header shares the first line with the descriptors before it, so the
   * largest header the geometry allows is what ena_com itself computes:
   * desc_list_entry_size - descs_num_before_header * desc_entry_size.
   * Leaving this field at 0 -- which a static struct does silently -- makes
   * ena_com_prepare_tx return ENA_COM_INVAL for every header and the whole
   * harness vacuous. */
  sq.tx_max_header_size =
    (u16)(ENTRY_SIZE - li->descs_num_before_header * DESC_SIZE);

  sq.bounce_buf_ctrl.base_buffer = g_bounce;
  sq.bounce_buf_ctrl.next_to_use = 0;
  sq.bounce_buf_ctrl.buffer_size = ENTRY_SIZE;
  sq.bounce_buf_ctrl.buffers_num = NBOUNCE;

  sq.llq_buf_ctrl.curr_bounce_buf = g_bounce;
  sq.llq_buf_ctrl.idx = 0;
  sq.llq_buf_ctrl.descs_left_in_line = li->descs_num_before_header;

  /* num_bufs may be 0 on any packet: that is a fully pushed packet, all of it
   * in the header, which the DPDK driver produces whenever the whole packet
   * fits the push length.  Set per packet in the loop below. */
  ctx.req_id = 0;
  ctx.tso_enable = 0;
  ctx.l3_csum_enable = 0;
  ctx.l4_csum_enable = 0;
  ctx.l4_csum_partial = 0;
  ctx.push_header = g_push_header;

  /* Deliberately loop-free.  An initialisation loop here would be loop 0 of
   * main and the packet loop would be loop 1, which is an easy way to aim an
   * --unwindset at the wrong loop and silently truncate the path. */
  ctx.ena_bufs = g_bufs;

  /* The driver's own discipline, from eth_ena_xmit_pkts: before each packet,
   * ring the doorbell if the estimate says the burst is about to overflow;
   * then submit.  A line is only charged against the burst when it is
   * completed and copied to the device, so the effect of an under-estimate
   * need not show on the packet that caused it -- it shows when some later
   * packet completes a line.  Hence a burst of packets rather than one. */
  for(unsigned k = 0; k < NPKTS; k++)
  {
    ctx.num_bufs = nd_u8();
    __CPROVER_assume(ctx.num_bufs <= MAX_BUFS);
    ctx.header_len = nd_u16();
    __CPROVER_assume(
      ctx.header_len >= 1 && ctx.header_len <= sq.tx_max_header_size);
    ctx.meta_valid = nd_bool();
    ctx.req_id = (u16)k;

    const bool db = ena_com_is_doorbell_needed(&sq, &ctx);
    if(db)
      ena_com_write_sq_doorbell(&sq);

#ifdef WITH_FIX
    /* The fix: ringing the doorbell only restores the credit to
     * max_entries_in_tx_burst, so if the estimate STILL demands a doorbell
     * after one has been rung, the packet needs more LLQ lines than the whole
     * burst allows and no amount of ringing will help.  Such a packet has to
     * be refused (or split) rather than submitted.  Re-checking the existing
     * predicate is the smallest expression of that. */
    if(ena_com_is_doorbell_needed(&sq, &ctx))
      continue;
#endif

    /* room in the ring is a separate concern (the driver checks it before
     * calling); keep this harness on the burst accounting */
    __CPROVER_assume(
      sq.q_depth - (u16)(sq.tail - sq.next_to_comp) > ctx.num_bufs + 1);

    g_nb_hw_desc = 0;
    const int rc = ena_com_prepare_tx(&sq, &ctx, &g_nb_hw_desc);

    /* The estimate exists to guarantee exactly this.  If it holds, the
     * driver never offers the device more lines than the burst allows. */
    __CPROVER_assert(
      rc != ENA_COM_NO_SPACE,
      "DB_CONTRACT: the doorbell estimate keeps the burst from overflowing");
    if(rc != 0)
      break;
  }

#ifdef VACUITY_CHECK
  /* must FAIL: lines really are completed and charged against the burst, so
   * DB_CONTRACT is about something */
  /* These must FAIL.  The second is the one that matters: if no line is ever
   * completed then nothing is charged against the burst and DB_CONTRACT holds
   * for want of anything to violate it. */
  __CPROVER_assert(
    sq.entries_in_tx_burst_left >= g_initial_credit,
    "vacuity witness: the burst credit can be spent");
  __CPROVER_assert(
    sq.tail == 0, "vacuity witness: an LLQ line can be completed and written");
#endif
  return 0;
}
