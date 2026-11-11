/*
 * Phase 8, pair 2: the admin-queue submit/complete handshake.
 *
 * The two sides take different locks -- or rather, only one takes any:
 *
 *   submit    __ena_com_submit_admin_cmd runs under admin_queue->q_lock
 *             (ena_com.c:313-324)
 *   complete  ena_com_handle_admin_completion, on the interrupt or alarm
 *             thread, takes NO lock at all (ena_com.c:489-524)
 *
 * so the control thread and the interrupt thread are not mutually excluded on
 * the comp_ctx array they share.
 *
 * Submit publishes the slot in this order (ena_com.c:253-259), where get_comp_ctxt with capture = true is what sets
 * occupied:
 *
 *     comp_ctx = get_comp_ctxt(admin_queue, cmd_id, true);
 *     comp_ctx->status    = ENA_CMD_SUBMITTED;
 *     comp_ctx->comp_size = comp_size_in_bytes;
 *     comp_ctx->user_cqe  = comp;
 *
 * `occupied` is set BEFORE `user_cqe`, and the completion handler gates on
 * `occupied` and then writes through `user_cqe`:
 *
 *     if (!comp_ctx->occupied) return;
 *     ...
 *     if (comp_ctx->user_cqe)
 *             memcpy(comp_ctx->user_cqe, (void *)cqe, comp_ctx->comp_size);
 *
 * The question this harness asks: can a completion see occupied == 1 while
 * user_cqe still holds the PREVIOUS command's buffer, which its waiter has
 * already released and which may be a dead stack frame?
 *
 * Obligations:
 *   CC2_NO_STALE_CQE  a completion never writes through a user_cqe whose
 *                     owning command has been released
 *   CC2_NO_LOST       a completion for an outstanding command is not lost
 *
 * WELL_BEHAVED_DEVICE restricts the device to completing only commands it was
 * actually told about, i.e. no duplicate or stale completions.  That is the
 * assumption under which the handshake is expected to be sound, and the
 * difference between the two configurations is the result.
 */
#include <stdbool.h>
#include <stdint.h>

static int nd_int(void) { int v; return v; }

/* one comp_ctx slot, the shared object */
struct slot
{
  bool occupied;
  int status; /* 0 = free, 1 = submitted, 2 = completed */
  void *user_cqe;
  unsigned comp_size;
};

#define BUF_LIVE 1
#define BUF_DEAD 2
static int buf_a_state, buf_b_state; /* the two response buffers */
static void *const BUF_A = (void *)0x1000;
static void *const BUF_B = (void *)0x2000;

static struct slot s;
static unsigned g_wrote_dead;

static void write_through(void *p)
{
  if((p == BUF_A && buf_a_state == BUF_DEAD) ||
     (p == BUF_B && buf_b_state == BUF_DEAD))
    g_wrote_dead++;
}

/* the completion handler's per-entry action, as ena_com.c:459-487 */
static void complete_step(void *cqe_for)
{
  (void)cqe_for;
  if(!s.occupied)
    return;
  s.status = 2;
  if(s.user_cqe)
    write_through(s.user_cqe);
}

int main(void)
{
  /* The previous command used BUF_A and has been released: its waiter
   * returned, so the buffer is gone, but user_cqe still points at it until
   * the next submit overwrites the field. */
  s.occupied = false;
  s.status = 0;
  s.user_cqe = BUF_A;
  s.comp_size = 8;
  buf_a_state = BUF_DEAD;
  buf_b_state = BUF_LIVE;
  g_wrote_dead = 0;

  /* the new submit, split into the three steps the source performs in order */
  unsigned pc = 0;
  bool submitted = false;
  bool completed_outstanding = false;
  /* the device has NOT been told about the new command until the doorbell,
   * which is after all three steps */
  bool doorbell_rung = false;

  for(unsigned step = 0; step < 6; step++)
  {
    const int who = nd_int();
    if(who == 0)
    {
      /* the control thread, under q_lock */
      if(pc == 0)
      {
        s.occupied = true; /* get_comp_ctxt(..., capture = true) */
        pc = 1;
      }
      else if(pc == 1)
      {
        s.status = 1; /* ENA_CMD_SUBMITTED */
        s.comp_size = 8;
        pc = 2;
      }
      else if(pc == 2)
      {
        s.user_cqe = BUF_B; /* the new command's buffer */
        pc = 3;
      }
      else if(pc == 3)
      {
        doorbell_rung = true; /* now the device knows */
        submitted = true;
        pc = 4;
      }
    }
    else if(who == 1)
    {
      /* the interrupt thread, no lock */
#ifdef WELL_BEHAVED_DEVICE
      /* the device completes only commands it was told about */
      if(!doorbell_rung)
        continue;
#endif
      complete_step(BUF_B);
      if(doorbell_rung)
        completed_outstanding = true;
    }
  }

  __CPROVER_assert(
    g_wrote_dead == 0,
    "CC2_NO_STALE_CQE: a completion never writes through a released "
    "command's response buffer");
  __CPROVER_assert(
    !submitted || !completed_outstanding || s.status == 2,
    "CC2_NO_LOST: a completion for an outstanding command is recorded");

#ifdef VACUITY_CHECK
  __CPROVER_assert(!submitted, "vacuity witness: the submit can complete");
  __CPROVER_assert(
    !completed_outstanding,
    "vacuity witness: the device can complete the new command");
#endif
  return 0;
}
