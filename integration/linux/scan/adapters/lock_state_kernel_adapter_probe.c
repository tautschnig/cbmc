/// \file
/// lock_state_kernel_adapter_probe.c — vacuity-probe variant of
/// lock_state_kernel_adapter.c.  The substantive precondition
/// is replaced with `__CPROVER_requires(0 == 1)`; scan.py links
/// this in place of the real adapter for a one-shot probe run
/// that MUST fail, proving the contract call site is reachable.

struct mutex;

void mutex_unlock(struct mutex *lock)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
