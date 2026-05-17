/// \file
/// cancel_work_before_free_kernel_adapter_probe.c — vacuity-probe.

struct work_struct;

void __assert_no_pending_work(struct work_struct *work)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
