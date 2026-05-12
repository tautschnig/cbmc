# cred_lifetime property module

Tracks the Linux kernel `struct cred` refcount to catch two
related bug patterns:

1. **Use after put.**  `put_cred(c)` may drop the refcount to
   zero and free the cred; any subsequent dereference is a
   use-after-free unless the caller held an additional
   reference.

2. **Leaked reference.**  A path that `get_cred(c)`s without a
   matching `put_cred(c)` leaks kernel memory.  The motivating
   concrete CVE is CVE-2026-23297 (`nfsd_nl_threads_set_doit`),
   where an error path skipped `put_cred` after
   `get_cred`-like handling.

## Files

- [`cred_lifetime.h`](cred_lifetime.h) — public API: a minimal
  kernel-layout-compatible `struct cred`, the ghost-state API,
  and the `cred_live` predicate.
- [`cred_lifetime.c`](cred_lifetime.c) — reference implementation
  of the ghost table, refcount tracking, and `cred_live`.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`cred_lifetime.cocci`](cred_lifetime.cocci) — Coccinelle
  prefilter that surfaces `put_cred(c); ... c->…` sequences.
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter: [`../../scan/adapters/cred_kernel_adapter.c`](../../scan/adapters/cred_kernel_adapter.c)
  attaches `__CPROVER_requires(cred_live(c) == 1)` to
  `put_cred` (the canonical kernel API whose `static inline`
  form is exposed under
  `__CPROVER_file_local_cred_h_put_cred`).
- Direct-call harness: [`../../scan/adapters/cred_kernel_direct_harness.c`](../../scan/adapters/cred_kernel_direct_harness.c)
  constructs a cred, drops its refcount via `put_cred`, then
  calls `put_cred` again.  The second call fires the contract
  precondition in the vulnerable shape; the fix shape holds
  an extra `get_cred` reference so the refcount stays positive.
