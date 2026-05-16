# fput_lifetime property module

Tracks the Linux kernel `struct file` refcount to catch two
related bug patterns:

1. **Double put.**  `fput(file)` decrements the
   refcount and, when it reaches zero, frees the object.  An error
   path that puts a `struct file` another path already put
   typically lands as UAF on the next caller's reference.

2. **Put without a matching get.**  Code obtains a reference by
   other means and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` / `kobject_lifetime`: a per-
pointer ghost counter tracks each `struct file`'s notional
refcount; `get_file` increments and `fput`
decrements; the `fput_live(file)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `fput`.

Motivating CVE class: file struct UAFs / fdput races (34+ CVE descriptions mention fput).
Subsystem focus: `fs/*`.

## Files

- [`fput_lifetime.h`](fput_lifetime.h) — public API.
- [`fput_lifetime.c`](fput_lifetime.c) — reference impl.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`fput_lifetime.cocci`](fput_lifetime.cocci) — Coccinelle
  prefilter (call-site + back-to-back-put bug shape).
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/fput_kernel_adapter.c`](../../scan/adapters/fput_kernel_adapter.c)
  attaches `__CPROVER_requires(fput_live(file) == 1)`
  to `fput`.
- Direct-call harness:
  [`../../scan/adapters/fput_kernel_direct_harness.c`](../../scan/adapters/fput_kernel_direct_harness.c)
  vuln/fix shape (init=1 vs init=2; second put fires
  precondition in vuln, holds in fix).
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `struct file *` with usage=1, so
  per-file scans on functions that take this type get a live
  ghost and produce real verdicts.
