/// \file
/// cancel_delayed_work_before_free_kernel_direct_harness.c
///
/// Vuln/fix shapes mirroring cancel_work_before_free's harness.

struct delayed_work
{
  int dummy;
};

void cancel_dwork_set_pending(struct delayed_work *dwork);
void cancel_dwork_clear_pending(struct delayed_work *dwork);
int cancel_dwork_pending(struct delayed_work *dwork);

void __assert_no_pending_dwork(struct delayed_work *dwork);

struct my_struct
{
  int data;
  struct delayed_work dwork;
};

int main(void)
{
  static struct my_struct x;
  struct my_struct *xp = &x;

  // INIT_DELAYED_WORK equivalent.
  cancel_dwork_set_pending(&xp->dwork);

#ifdef FIXED
  cancel_dwork_clear_pending(&xp->dwork);
#endif

  __assert_no_pending_dwork(&xp->dwork);
  return 0;
}
