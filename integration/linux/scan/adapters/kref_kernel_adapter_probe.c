/// \file
/// kref_kernel_adapter_probe.c — vacuity-probe
/// variant of kref_kernel_adapter.c.  The
/// substantive precondition is replaced with
/// `__CPROVER_requires(0 == 1)`.  scan.py links this in place of
/// the real adapter for a one-shot probe run that MUST fail,
/// proving the contract call site is reachable.

struct kref;

int __CPROVER_file_local_kref_h_kref_put(struct kref *kref, void (*release)(struct kref *kref))
  __CPROVER_requires(0 == 1) __CPROVER_assigns();


int kref_put(struct kref *kref, void (*release)(struct kref *kref))
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
