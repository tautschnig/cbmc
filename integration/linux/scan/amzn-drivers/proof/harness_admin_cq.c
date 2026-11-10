/*
 * Async operation 3: the admin completion queue, the other half of
 * ena_control_path_handler.
 *
 * ena_com_admin_q_comp_intr_handler -> ena_com_handle_admin_completion walks
 * a ring of device-written completion descriptors.  Each descriptor's
 * `command` field selects which pending command it completes
 * (ena_com.c:465), so as on RX and the AENQ the device chooses which driver
 * object is touched.  get_comp_ctxt (ena_com.c:192-206) is what bounds it.
 *
 * The per-completion action is
 *
 *     memcpy(comp_ctx->user_cqe, (void *)cqe, comp_ctx->comp_size);
 *
 * where user_cqe and comp_size were recorded when the command was submitted.
 * So the device cannot pick the length, but it can pick WHICH pending
 * command's buffer is written, which is what the obligations below are about.
 *
 * Obligations:
 *   AC_HEAD        no more completions are consumed than the ring holds
 *   AC_PHASE       the phase bit flips exactly on a ring wrap (class F: a
 *                  lost or re-read completion)
 *   AC_HEADS_AGREE the SQ and CQ heads advance together, which is what keeps
 *                  the free-slot accounting of the admin queue honest
 *   AC_UNOCCUPIED  a completion aimed at a slot with no command outstanding
 *                  changes nothing -- the device cannot fabricate a reply
 *   plus --bounds-check --pointer-check over the ring walk, the comp_ctx
 *   lookup and the memcpy into the submitter's buffer
 *
 * Switches: QD (admin queue depth), VACUITY_CHECK.
 */
#include "../dpdk/drivers/net/ena/base/ena_plat_dpdk.h"
#include "../dpdk/drivers/net/ena/base/ena_com.c"

#ifndef QD
#define QD 4
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_com_dev ena_dev;
static struct ena_admin_acq_entry cq_ring[QD];
static struct ena_comp_ctx comp_ctxs[QD];
/* each pending command's response buffer, exactly as large as the entry the
 * device writes, which is what comp_size records at submit time */
static struct ena_admin_acq_entry user_cqes[QD];

int ena_logtype_com = 0;
int rte_log(uint32_t l, uint32_t t, const char *f, ...)
{
  (void)l;
  (void)t;
  (void)f;
  return 0;
}

int main(void)
{
  unsigned i;
  struct ena_com_admin_queue *aq = &ena_dev.admin_queue;

  aq->ena_dev = &ena_dev;
  aq->q_depth = QD;
  aq->comp_ctx = comp_ctxs;
  aq->cq.entries = cq_ring;
  aq->polling = nd_int() ? true : false;
  aq->running_state = true;
  aq->cq.head = nd_u16();
  aq->cq.phase = nd_u8() & 1;
  aq->sq.head = nd_u16();
  aq->stats.completed_cmd = nd_u32();

  const uint16_t masked_head = (uint16_t)(aq->cq.head & (QD - 1));
  const uint16_t pending = nd_u16() % (QD + 1);

  /* the pending commands, as ena_com_init_comp_ctxt and the submit path
   * leave them: each either has a command outstanding or does not */
  for(i = 0; i < QD; i++)
  {
    comp_ctxs[i].occupied = nd_int() ? true : false;
    comp_ctxs[i].status = ENA_CMD_SUBMITTED;
    comp_ctxs[i].comp_status = nd_u8();
    comp_ctxs[i].user_cqe =
      nd_int() ? NULL : (struct ena_admin_acq_entry *)&user_cqes[i];
    /* comp_size is recorded by the submitter, never by the device */
    comp_ctxs[i].comp_size = (uint32_t)sizeof(struct ena_admin_acq_entry);
  }

  /* the completion ring is arbitrary device memory, except that the phase
   * bits follow the protocol: a prefix of `pending` entries carries the
   * phase the driver expects at that distance, the rest its complement.
   * The driver flips its own phase on each wrap (ena_com.c:512-515), so the
   * expected phase depends on the number of wraps -- the same correction the
   * AENQ harness needed. */
  for(i = 0; i < QD; i++)
  {
    const uint16_t d = (uint16_t)((i + QD - masked_head) % QD);
    const uint16_t wraps = (uint16_t)((masked_head + d) / QD);
    const uint8_t expected = (uint8_t)((aq->cq.phase ^ (wraps & 1)) & 1);
    const uint8_t ph = (uint8_t)(d < pending ? expected : !expected);
    uint8_t fl = nd_u8();
    fl = (uint8_t)((fl & (uint8_t)~ENA_ADMIN_ACQ_COMMON_DESC_PHASE_MASK) |
                   (ph ? ENA_ADMIN_ACQ_COMMON_DESC_PHASE_MASK : 0));
    cq_ring[i].acq_common_descriptor.flags = fl;
    /* the device picks which command each entry claims to complete */
    cq_ring[i].acq_common_descriptor.command = nd_u16();
    cq_ring[i].acq_common_descriptor.status = nd_u8();
    cq_ring[i].acq_common_descriptor.extended_status = nd_u16();
    cq_ring[i].acq_common_descriptor.sq_head_indx = nd_u16();
  }

  /* a probe slot that has NO command outstanding: the device must not be
   * able to make it look completed */
  const uint16_t probe = nd_u16() % QD;
  __CPROVER_assume(!comp_ctxs[probe].occupied);
  const enum ena_cmd_status probe_status = comp_ctxs[probe].status;

  const uint16_t cq_head0 = aq->cq.head;
  const uint16_t sq_head0 = aq->sq.head;
  const uint8_t phase0 = aq->cq.phase;

  /* ---- one admin-completion interrupt ---- */
  ena_com_admin_q_comp_intr_handler(&ena_dev);

  /* ---- obligations ---- */
  const uint16_t consumed = (uint16_t)(aq->cq.head - cq_head0);
  __CPROVER_assert(
    consumed <= QD, "AC_HEAD: no more completions are consumed than the ring holds");
  const uint16_t start_masked = (uint16_t)(cq_head0 & (QD - 1));
  const bool wrapped = (uint16_t)(start_masked + consumed) >= QD;
  __CPROVER_assert(
    (aq->cq.phase != phase0) == wrapped,
    "AC_PHASE: the phase bit flips exactly when the walk wraps the ring");
  __CPROVER_assert(
    (uint16_t)(aq->sq.head - sq_head0) == consumed,
    "AC_HEADS_AGREE: the SQ and CQ heads advance by the same amount");
  __CPROVER_assert(
    comp_ctxs[probe].status == probe_status,
    "AC_UNOCCUPIED: a completion for a slot with no command outstanding "
    "changes nothing");

#ifdef VACUITY_CHECK
  __CPROVER_assert(consumed == 0, "vacuity witness: a completion can be consumed");
  __CPROVER_assert(
    aq->cq.phase == phase0, "vacuity witness: the walk can wrap the ring");
  __CPROVER_assert(
    aq->running_state,
    "vacuity witness: a bad command id can stop the admin queue");
#endif
  return 0;
}
