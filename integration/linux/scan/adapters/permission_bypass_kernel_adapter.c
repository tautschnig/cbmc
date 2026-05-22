/// \file
/// permission_bypass_kernel_adapter.c

int cap_was_checked(void);

void __assert_privileged(void) __CPROVER_requires(cap_was_checked() == 1)
  __CPROVER_assigns();
