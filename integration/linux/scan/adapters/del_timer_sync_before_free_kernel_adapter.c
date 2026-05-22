/// \file
/// del_timer_sync_before_free_kernel_adapter.c

struct timer_list;

int timer_armed(struct timer_list *t);

void __assert_no_armed_timer(struct timer_list *t)
  __CPROVER_requires(timer_armed(t) == 0) __CPROVER_assigns();
