# module_lifetime property module

Tracks the Linux kernel `struct module` refcount to catch two
related bug patterns:

1. **Double put.**  `module_put(module)` decrements the
   refcount and, when it reaches zero, frees the object.  An error
   path that puts a `struct module` another path already put
   typically lands as UAF on the next caller's reference.

2. **Put without a matching get.**  Code obtains a reference by
   other means and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` / `kobject_lifetime`: a per-
pointer ghost counter tracks each `struct module`'s notional
refcount; `try_module_get` increments and `module_put`
decrements; the `module_live(module)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `module_put`.

Motivating CVE class: module reference leaks blocking module unload (11+ CVE descriptions mention try_module_get).
Subsystem focus: `kernel/, drivers/*`.

## Files

- [`module_lifetime.h`](module_lifetime.h) — public API.
- [`module_lifetime.c`](module_lifetime.c) — reference impl.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`module_lifetime.cocci`](module_lifetime.cocci) — Coccinelle
  prefilter (call-site + back-to-back-put bug shape).
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/module_kernel_adapter.c`](../../scan/adapters/module_kernel_adapter.c)
  attaches `__CPROVER_requires(module_live(module) == 1)`
  to `module_put`.
- Direct-call harness:
  [`../../scan/adapters/module_kernel_direct_harness.c`](../../scan/adapters/module_kernel_direct_harness.c)
  vuln/fix shape (init=1 vs init=2; second put fires
  precondition in vuln, holds in fix).
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `struct module *` with usage=1, so
  per-file scans on functions that take this type get a live
  ghost and produce real verdicts.
