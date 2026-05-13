# refcount_lifetime property module

Tracks `refcount_t` counters used across the Linux kernel
(introduced as a safer replacement for raw `atomic_t`
refcounts) to catch underflow / double-decrement bugs and
leaked-reference bugs.

## Bug class

- **Underflow double-dec.**  A path calls
  `refcount_dec_and_test(r)` on an already-zero counter.  The
  `refcount_t` API saturates rather than wraps, but the
  object was freed on the first dec — the second dec is the
  UAF.  io_uring, crypto/keysetup, sk_buff, and bpf have all
  had real CVE-class underflow bugs of this shape.
- **Leaked reference.**  A path that `refcount_inc(r)`s
  without a matching `refcount_dec_and_test(r)` leaks kernel
  memory.

## Files

- [`refcount_lifetime.h`](refcount_lifetime.h) — public API:
  opaque forward-decl of `refcount_t`, the ghost-state API,
  and the `refcount_live` predicate.
- [`refcount_lifetime.c`](refcount_lifetime.c) — reference
  implementation of the ghost table, refcount tracking, and
  `refcount_live`.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`refcount_lifetime.cocci`](refcount_lifetime.cocci) —
  Coccinelle prefilter that surfaces
  `refcount_dec_and_test(r)` call sites.
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

See [`../../scan/adapters/refcount_kernel_adapter.c`](../../scan/adapters/refcount_kernel_adapter.c):
attaches `__CPROVER_requires(refcount_live(r) == 1)` to
`refcount_dec_and_test`.  The direct-call harness under
[`../../scan/adapters/refcount_kernel_direct_harness.c`](../../scan/adapters/refcount_kernel_direct_harness.c)
exercises the double-dec shape in the vulnerable direction and
an inc/dec pair in the fix direction.

## Why another refcount-shaped module?

This module is deliberately a template-rehash of
`cred_lifetime`: same ghost-state pattern, same predicate
shape, different key type (`refcount_t *` rather than
`struct cred *`).  The pragmatic reason is kernel coverage:
`refcount_t` is used by *thousands* of kernel objects (io_uring
requests, sk_buffs, bpf maps, keyrings, …), whereas
`cred_lifetime` covers only the `struct cred` API.  Keeping
both modules separate lets the scan apply tighter
`refcount_t`-specific preconditions without polluting
`cred_lifetime`'s contract.

For a more comprehensive scale-out, future modules can
replicate this template for `kref_get`/`kref_put`,
`percpu_ref_get`/`percpu_ref_put`, and `atomic_long_dec_and_test`
by copying this directory and renaming.
