/// \file
/// tocttou_inode_check_kernel_adapter.c — adapter contracts.
///
/// Three contracts:
///
///   __tic_check     — pure read; returns __tic_state via
///                     __CPROVER_ensures.
///   __tic_act       — requires __tic_state == checked
///                     (i.e. captured value still matches).
///   __tic_change    — assigns + ensures __tic_state to the
///                     new value (concurrent mutator).

extern unsigned int __tic_state;

unsigned int __tic_check(void) __CPROVER_assigns()
  __CPROVER_ensures(__CPROVER_return_value == __tic_state);

void __tic_act(unsigned int checked) __CPROVER_requires(__tic_state == checked)
  __CPROVER_assigns();

void __tic_change(unsigned int new_state) __CPROVER_assigns(__tic_state)
  __CPROVER_ensures(__tic_state == new_state);
