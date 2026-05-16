# device_lifetime property module

Tracks the Linux kernel `struct device` refcount to catch two
related bug patterns:

1. **Double put.**  `put_device(dev)` decrements the
   refcount and, when it reaches zero, frees the object.  An error
   path that puts a `struct device` another path already put
   typically lands as UAF on the next caller's reference.

2. **Put without a matching get.**  Code obtains a reference by
   other means and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` / `kobject_lifetime`: a per-
pointer ghost counter tracks each `struct device`'s notional
refcount; `get_device` increments and `put_device`
decrements; the `device_live(dev)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `put_device`.

Motivating CVE class: driver-core UAFs (CVE family: 82+ CVE descriptions mention put_device, the most-cited refcount API in the 2023-2026 kernel CVE record).
Subsystem focus: `drivers/*`.

## Files

- [`device_lifetime.h`](device_lifetime.h) — public API.
- [`device_lifetime.c`](device_lifetime.c) — reference impl.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`device_lifetime.cocci`](device_lifetime.cocci) — Coccinelle
  prefilter (call-site + back-to-back-put bug shape).
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/device_kernel_adapter.c`](../../scan/adapters/device_kernel_adapter.c)
  attaches `__CPROVER_requires(device_live(dev) == 1)`
  to `put_device`.
- Direct-call harness:
  [`../../scan/adapters/device_kernel_direct_harness.c`](../../scan/adapters/device_kernel_direct_harness.c)
  vuln/fix shape (init=1 vs init=2; second put fires
  precondition in vuln, holds in fix).
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `struct device *` with usage=1, so
  per-file scans on functions that take this type get a live
  ghost and produce real verdicts.
