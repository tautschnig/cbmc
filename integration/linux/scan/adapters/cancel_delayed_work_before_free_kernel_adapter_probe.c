/// \file
/// cancel_delayed_work_before_free_kernel_adapter_probe.c

struct delayed_work;

void __assert_no_pending_dwork(struct delayed_work *dwork)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
