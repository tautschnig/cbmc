# inode_lifetime property module

Tracks the Linux kernel `struct inode` refcount to catch two
related bug patterns:

1. **Double put.**  `iput(inode)` decrements the
   refcount and, when it reaches zero, frees the object.  An error
   path that puts a `struct inode` another path already put
   typically lands as UAF on the next caller's reference.

2. **Put without a matching get.**  Code obtains a reference by
   other means and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` / `kobject_lifetime`: a per-
pointer ghost counter tracks each `struct inode`'s notional
refcount; `ihold` increments and `iput`
decrements; the `inode_live(inode)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `iput`.

Motivating CVE class: filesystem inode UAFs (50+ CVE descriptions mention iput).
Subsystem focus: `fs/*`.

## Files

- [`inode_lifetime.h`](inode_lifetime.h) — public API.
- [`inode_lifetime.c`](inode_lifetime.c) — reference impl.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`inode_lifetime.cocci`](inode_lifetime.cocci) — Coccinelle
  prefilter (call-site + back-to-back-put bug shape).
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/inode_kernel_adapter.c`](../../scan/adapters/inode_kernel_adapter.c)
  attaches `__CPROVER_requires(inode_live(inode) == 1)`
  to `iput`.
- Direct-call harness:
  [`../../scan/adapters/inode_kernel_direct_harness.c`](../../scan/adapters/inode_kernel_direct_harness.c)
  vuln/fix shape (init=1 vs init=2; second put fires
  precondition in vuln, holds in fix).
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `struct inode *` with usage=1, so
  per-file scans on functions that take this type get a live
  ghost and produce real verdicts.
