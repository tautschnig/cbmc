/// \file
/// del_timer_sync_before_free_kernel_adapter_probe.c

struct timer_list;

void __assert_no_armed_timer(struct timer_list *t) __CPROVER_requires(0 == 1)
  __CPROVER_assigns();
