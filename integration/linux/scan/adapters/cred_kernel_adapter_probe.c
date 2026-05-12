/// \file
/// cred_kernel_adapter_probe.c — vacuity-probe variant of
/// cred_kernel_adapter.c.  The substantive precondition is
/// replaced with `__CPROVER_requires(0 == 1)`; scan.py links
/// this in place of the real adapter for a one-shot probe run
/// that MUST fail, proving the contract call site is reachable.

struct cred;

void __CPROVER_file_local_cred_h_put_cred(const struct cred *_cred)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();

void put_cred(const struct cred *_cred)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
