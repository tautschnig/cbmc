/*
 * Class R: a response attributed to the wrong request.
 *
 * Two of the forty class Z findings are this shape, and the class list had no
 * name for it:
 *
 *   "Wrong admin response returned after abort and command-ID reuse"
 *       (shared ena_com)
 *   "An admin command returns another command's response when the device
 *       duplicates a completion"  (Linux EFA)
 *
 * harness_admin_cq.c already proves that a completion aimed at a slot with no
 * command outstanding changes nothing (AC_UNOCCUPIED), which is the easy half:
 * the driver checks `occupied`.  This harness is about the hard half, where
 * the slot IS occupied, but by a DIFFERENT command than the one the
 * completion belongs to.
 *
 * The mechanism, from the code:
 *
 *   * a command's only identity is its slot index.  __ena_com_submit_admin_cmd
 *     takes `cmd_id = admin_queue->curr_cmd_id` and advances that counter
 *     modulo the queue depth (ena_com.c:245-267), so ids are reused as soon
 *     as the ring wraps;
 *   * ena_com_handle_single_admin_completion looks the completion up by that
 *     id alone and, if the slot is occupied, copies the device's entry into
 *     whatever buffer the slot currently names (ena_com.c:459-487);
 *   * ena_com_abort_admin_commands sets status = ENA_CMD_ABORTED but leaves
 *     `occupied` set and `user_cqe` pointing at the abandoned submitter's
 *     buffer (ena_com.c:2237-2254).
 *
 * Nothing in a completion descriptor distinguishes the first user of an id
 * from the second.  So a completion that arrives late -- after its command was
 * abandoned and the id handed to a new command -- is delivered to the new
 * command's buffer, carrying the old command's status.
 *
 * The harness gives each submission a ghost GENERATION, which the hardware
 * has no field for, and asks whether what lands in a submitter's buffer is a
 * reply to that submitter's own request:
 *
 *   AR_RIGHT_REQUEST  a completion written into a slot's buffer carries the
 *                     generation of the command currently in that slot
 *   AR_NO_STALE_STATUS a submitter does not observe a status belonging to an
 *                     earlier user of its command id
 *
 * -DWITH_GENERATION_CHECK models the fix: the driver records a generation per
 *  slot and ignores a completion whose generation does not match. It cannot be
 *  implemented for real without a protocol field, which is the point -- the
 *  fix has to be "do not reuse an id while a completion may still arrive", and
 *  the configuration shows which obligation that restores.
 *
 * Switches: VACUITY_CHECK, WITH_GENERATION_CHECK.
 */
#include "../dpdk/drivers/net/ena/base/ena_plat_dpdk.h"
#include "../dpdk/drivers/net/ena/base/ena_com.c"

#define QD 4

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_com_dev ena_dev;
static struct ena_admin_acq_entry cq_ring[QD];
static struct ena_comp_ctx comp_ctxs[QD];
static struct ena_admin_acq_entry buf_first;  /* the abandoned submitter's */
static struct ena_admin_acq_entry buf_second; /* the new command's */

/* ---- ghost: the generation of the command occupying each slot ---- */
static unsigned g_slot_gen[QD];
/* the generation the completion in flight belongs to */
static unsigned g_cqe_gen;

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
  struct ena_com_admin_queue *aq = &ena_dev.admin_queue;

  aq->ena_dev = &ena_dev;
  aq->q_depth = QD;
  aq->comp_ctx = comp_ctxs;
  aq->cq.entries = cq_ring;
  aq->polling = true;
  aq->running_state = true;
  aq->cq.phase = 1;
  aq->cq.head = 0;
  aq->sq.head = 0;

  /* the command id both commands will use: the device picks which id a
   * completion names, and the driver reuses ids modulo the depth */
  const uint16_t id = nd_u16() % QD;

  /* ---- the first command: submitted, then abandoned ----
   *
   * ena_com_abort_admin_commands sets the status and signals the waiter, but
   * leaves occupied set and user_cqe pointing here.  Whether the waiter then
   * releases the slot is the question the two configurations below separate. */
  g_slot_gen[id] = 1;
  comp_ctxs[id].occupied = true;
  comp_ctxs[id].status = ENA_CMD_SUBMITTED;
  comp_ctxs[id].user_cqe = &buf_first;
  comp_ctxs[id].comp_size = (uint32_t)sizeof(struct ena_admin_acq_entry);
  ena_com_abort_admin_commands(&ena_dev);

  /* the abandoned waiter releases its context, as the wait path does on its
   * way out, which frees the id for reuse */
  comp_ctxs[id].user_cqe = NULL;
  comp_ctxs[id].occupied = false;

  /* ---- the second command: takes the same id ---- */
  g_slot_gen[id] = 2;
  comp_ctxs[id].occupied = true;
  comp_ctxs[id].status = ENA_CMD_SUBMITTED;
  comp_ctxs[id].comp_status = 0;
  comp_ctxs[id].user_cqe = &buf_second;
  comp_ctxs[id].comp_size = (uint32_t)sizeof(struct ena_admin_acq_entry);
  buf_second.acq_common_descriptor.status = 0;

  /* ---- the device delivers the FIRST command's completion, late ----
   *
   * It names the id, which is all a completion descriptor carries, and a
   * status of its own.  The generation is a ghost: no protocol field holds
   * it, which is exactly why the driver cannot tell the two apart. */
  g_cqe_gen = 1;
  const uint8_t stale_status = 0x7f;
  cq_ring[0].acq_common_descriptor.command =
    (uint16_t)(id & ENA_ADMIN_ACQ_COMMON_DESC_COMMAND_ID_MASK);
  cq_ring[0].acq_common_descriptor.flags = aq->cq.phase & 1;
  cq_ring[0].acq_common_descriptor.status = stale_status;
  /* the rest of the ring carries the opposite phase, so exactly one
   * completion is consumed */
  for(unsigned k = 1; k < QD; k++)
    cq_ring[k].acq_common_descriptor.flags = (uint8_t)(!(aq->cq.phase & 1));

#ifdef WITH_GENERATION_CHECK
  /* The fix, modelled: refuse a completion whose generation does not match
   * the slot's.  No protocol field carries the generation, so a real driver
   * cannot do this -- it has to avoid reusing an id while a completion may
   * still arrive.  The configuration exists to show which obligation that
   * would restore. */
  if(g_cqe_gen == g_slot_gen[id])
    ena_com_admin_q_comp_intr_handler(&ena_dev);
#else
  ena_com_admin_q_comp_intr_handler(&ena_dev);
#endif

  /* ---- the obligations ---- */
  __CPROVER_assert(
    buf_second.acq_common_descriptor.status != stale_status,
    "AR_NO_STALE_STATUS: a submitter does not observe a status belonging to "
    "an earlier user of its command id");
  __CPROVER_assert(
    g_cqe_gen == g_slot_gen[id] ||
      buf_second.acq_common_descriptor.status == 0,
    "AR_RIGHT_REQUEST: a completion written into a slot's buffer carries the "
    "generation of the command currently in that slot");

#ifdef VACUITY_CHECK
  /* Both must FAIL: the second command must really be occupying the id, and
   * the stale completion must really be delivered, or the obligations hold
   * for want of a scenario. */
  __CPROVER_assert(
    !comp_ctxs[id].occupied,
    "witness: the reused id is occupied by the second command");
  __CPROVER_assert(
    g_slot_gen[id] != 2, "witness: the second command's generation is current");
#endif
  return 0;
}
