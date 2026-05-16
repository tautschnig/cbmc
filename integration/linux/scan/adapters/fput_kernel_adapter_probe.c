/// \file
/// fput_kernel_adapter_probe.c — vacuity-probe
/// variant of fput_kernel_adapter.c.  The
/// substantive precondition is replaced with
/// `__CPROVER_requires(0 == 1)`.  scan.py links this in place of
/// the real adapter for a one-shot probe run that MUST fail,
/// proving the contract call site is reachable.

struct file;

void fput(struct file *file)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
