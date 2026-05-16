# of_node_lifetime property module

Tracks the Linux kernel `struct device_node` refcount to catch two
related bug patterns:

1. **Double put.**  `of_node_put(node)` decrements the
   refcount and, when it reaches zero, frees the object.  An error
   path that puts a `struct device_node` another path already put
   typically lands as UAF on the next caller's reference.

2. **Put without a matching get.**  Code obtains a reference by
   other means and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` / `kobject_lifetime`: a per-
pointer ghost counter tracks each `struct device_node`'s notional
refcount; `of_node_get` increments and `of_node_put`
decrements; the `of_node_live(node)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `of_node_put`.

Motivating CVE class: Open Firmware / device tree refcount UAFs (55+ CVE descriptions mention of_node_put).
Subsystem focus: `drivers/of, drivers/*`.

## Files

- [`of_node_lifetime.h`](of_node_lifetime.h) — public API.
- [`of_node_lifetime.c`](of_node_lifetime.c) — reference impl.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`of_node_lifetime.cocci`](of_node_lifetime.cocci) — Coccinelle
  prefilter (call-site + back-to-back-put bug shape).
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/of_node_kernel_adapter.c`](../../scan/adapters/of_node_kernel_adapter.c)
  attaches `__CPROVER_requires(of_node_live(node) == 1)`
  to `of_node_put`.
- Direct-call harness:
  [`../../scan/adapters/of_node_kernel_direct_harness.c`](../../scan/adapters/of_node_kernel_direct_harness.c)
  vuln/fix shape (init=1 vs init=2; second put fires
  precondition in vuln, holds in fix).
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `struct device_node *` with usage=1, so
  per-file scans on functions that take this type get a live
  ghost and produce real verdicts.
