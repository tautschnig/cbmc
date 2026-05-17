/// \file
/// cancel_work_before_free_kernel_direct_harness.c — direct-call
/// harness mirroring the cancel-before-free bug shape.
///
/// ## Vulnerable shape (default)
///
///   1. A struct containing a work_struct is allocated.
///   2. INIT_WORK-equivalent: harness calls
///      `cancel_work_set_pending(&x->work)`.
///   3. (Skipping cancel_work_sync — the bug.)
///   4. Checkpoint: `__assert_no_pending_work(&x->work)` — the
///      contract precondition `cancel_work_pending(work) == 0`
///      MUST FIRE because we never cleared pending.
///   5. (kfree(x) modelled as a no-op since CBMC's malloc-free
///      checking is orthogonal.)
///
/// ## Safe shape (`-DFIXED`)
///
///   1-2 same.
///   3. cancel_work_sync-equivalent: harness calls
///      `cancel_work_clear_pending(&x->work)`.
///   4. Checkpoint: precondition holds.

struct work_struct
{
  int dummy;
};

void cancel_work_set_pending(struct work_struct *work);
void cancel_work_clear_pending(struct work_struct *work);
int cancel_work_pending(struct work_struct *work);

void __assert_no_pending_work(struct work_struct *work);

struct my_struct
{
  int data;
  struct work_struct work;
};

int main(void)
{
  static struct my_struct x;
  struct my_struct *xp = &x;

  // INIT_WORK equivalent.
  cancel_work_set_pending(&xp->work);

#ifdef FIXED
  // Cancel pending work BEFORE the checkpoint.
  cancel_work_clear_pending(&xp->work);
#endif

  // Checkpoint just before the implicit kfree(xp).  Contract
  // requires cancel_work_pending(&xp->work) == 0.
  __assert_no_pending_work(&xp->work);

  return 0;
}
