# dentry_lifetime property module

Tracks the Linux kernel `struct dentry` refcount to catch two
related bug patterns:

1. **Double put.**  `dput(dentry)` decrements the
   refcount and, when it reaches zero, frees the object.  An error
   path that puts a `struct dentry` another path already put
   typically lands as UAF on the next caller's reference.

2. **Put without a matching get.**  Code obtains a reference by
   other means and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` / `kobject_lifetime`: a per-
pointer ghost counter tracks each `struct dentry`'s notional
refcount; `dget` increments and `dput`
decrements; the `dentry_live(dentry)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `dput`.

Motivating CVE class: filesystem dentry UAFs (47+ CVE descriptions mention dput).
Subsystem focus: `fs/*`.

## Files

- [`dentry_lifetime.h`](dentry_lifetime.h) — public API.
- [`dentry_lifetime.c`](dentry_lifetime.c) — reference impl.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`dentry_lifetime.cocci`](dentry_lifetime.cocci) — Coccinelle
  prefilter (call-site + back-to-back-put bug shape).
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/dentry_kernel_adapter.c`](../../scan/adapters/dentry_kernel_adapter.c)
  attaches `__CPROVER_requires(dentry_live(dentry) == 1)`
  to `dput`.
- Direct-call harness:
  [`../../scan/adapters/dentry_kernel_direct_harness.c`](../../scan/adapters/dentry_kernel_direct_harness.c)
  vuln/fix shape (init=1 vs init=2; second put fires
  precondition in vuln, holds in fix).
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `struct dentry *` with usage=1, so
  per-file scans on functions that take this type get a live
  ghost and produce real verdicts.
