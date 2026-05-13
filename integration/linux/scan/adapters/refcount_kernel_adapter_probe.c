/// \file
/// refcount_kernel_adapter_probe.c — vacuity-probe variant of
/// refcount_kernel_adapter.c.  The substantive precondition
/// is replaced with `__CPROVER_requires(0 == 1)`; scan.py links
/// this in place of the real adapter for a one-shot probe run
/// that MUST fail, proving the contract call site is
/// reachable.  Covers the external name plus both mangled
/// static-inline forms.

typedef struct refcount_struct refcount_t;

_Bool refcount_dec_and_test(refcount_t *r) __CPROVER_requires(0 == 1)
  __CPROVER_assigns();

_Bool __CPROVER_file_local_refcount_h_refcount_dec_and_test(refcount_t *r)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();

_Bool __CPROVER_file_local_refcount_h___refcount_dec_and_test(
  refcount_t *r,
  int *oldp) __CPROVER_requires(0 == 1) __CPROVER_assigns();
