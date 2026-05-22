/// \file
/// tocttou_inode_check_kernel_adapter_probe.c — vacuity-probe.

extern unsigned int __tic_state;

unsigned int __tic_check(void) __CPROVER_requires(0 == 1) __CPROVER_assigns();

void __tic_act(unsigned int checked) __CPROVER_requires(0 == 1)
  __CPROVER_assigns();

void __tic_change(unsigned int new_state) __CPROVER_requires(0 == 1)
  __CPROVER_assigns();
