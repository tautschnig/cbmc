# kref_lifetime property module

Tracks the Linux kernel `struct kref` refcount to catch two
related bug patterns:

1. **Double put.**  `kref_put(kref)` decrements the
   refcount and, when it reaches zero, frees the object.  An error
   path that puts a `struct kref` another path already put
   typically lands as UAF on the next caller's reference.

2. **Put without a matching get.**  Code obtains a reference by
   other means and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` / `kobject_lifetime`: a per-
pointer ghost counter tracks each `struct kref`'s notional
refcount; `kref_get` increments and `kref_put`
decrements; the `kref_live(kref)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `kref_put`.

Motivating CVE class: generic kref UAFs (30+ CVE descriptions mention kref_put; kref is the foundational primitive that many type-specific lifetime modules wrap).
Subsystem focus: `kernel/, drivers/*, fs/*`.

## Files

- [`kref_lifetime.h`](kref_lifetime.h) — public API.
- [`kref_lifetime.c`](kref_lifetime.c) — reference impl.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`kref_lifetime.cocci`](kref_lifetime.cocci) — Coccinelle
  prefilter (call-site + back-to-back-put bug shape).
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/kref_kernel_adapter.c`](../../scan/adapters/kref_kernel_adapter.c)
  attaches `__CPROVER_requires(kref_live(kref) == 1)`
  to `kref_put`.  Because `kref_put` is
  `static inline` in `<linux/kref.h>` the adapter
  attaches the contract to both the unmangled name and the
  goto-cc `--export-file-local-symbols` mangled form
  `__CPROVER_file_local_kref_h_kref_put` so it applies in every kernel TU that
  includes the header.
- Direct-call harness:
  [`../../scan/adapters/kref_kernel_direct_harness.c`](../../scan/adapters/kref_kernel_direct_harness.c)
  vuln/fix shape (init=1 vs init=2; second put fires
  precondition in vuln, holds in fix).
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `struct kref *` with usage=1, so
  per-file scans on functions that take this type get a live
  ghost and produce real verdicts.
