/// \file
/// cancel_delayed_work_before_free_kernel_adapter.c

struct delayed_work;

int cancel_dwork_pending(struct delayed_work *dwork);

void __assert_no_pending_dwork(struct delayed_work *dwork)
  __CPROVER_requires(cancel_dwork_pending(dwork) == 0) __CPROVER_assigns();
