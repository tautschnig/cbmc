# kobject_lifetime property module

Tracks the Linux kernel `struct kobject` refcount to catch two
related bug patterns:

1. **Double put.**  `kobject_put(k)` decrements the refcount and,
   when it reaches zero, frees `k` via the type's release
   function.  An error path that puts a kobject another path
   already put drops the refcount below the live threshold and
   typically frees an object some other code path still holds.

2. **Put without a matching get.**  Code that obtains a kobject
   reference by other means (e.g. `container_of` on a sysfs
   pointer the framework has not `kobject_get`ed for the caller)
   and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` exactly: a per-pointer ghost
counter tracks each kobject's notional refcount; `kobject_get`
increments and `kobject_put` decrements; the `kobject_live(k)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `kobject_put`.

## Files

- [`kobject_lifetime.h`](kobject_lifetime.h) — public API: a
  forward declaration of `struct kobject`, the ghost-state API,
  and the `kobject_live` predicate.
- [`kobject_lifetime.c`](kobject_lifetime.c) — reference
  implementation of the ghost table, refcount tracking, and
  `kobject_live`.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`kobject_lifetime.cocci`](kobject_lifetime.cocci) —
  Coccinelle prefilter that surfaces `kobject_put` and
  `kobject_del` call sites plus the back-to-back-put bug shape.
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/kobject_kernel_adapter.c`](../../scan/adapters/kobject_kernel_adapter.c)
  attaches `__CPROVER_requires(kobject_live(k) == 1)` to
  `kobject_put`.  Unlike `put_cred`, `kobject_put` is exported
  from `lib/kobject.c` (not `static inline`) so only the
  external-name form is needed.
- Direct-call harness:
  [`../../scan/adapters/kobject_kernel_direct_harness.c`](../../scan/adapters/kobject_kernel_direct_harness.c)
  constructs a kobject sentinel, drops its ghost refcount via
  `kobject_put`, then calls `kobject_put` again.  The second
  call fires the contract precondition in the vulnerable shape;
  the `-DFIXED` shape initialises with usage=2 so both puts
  land on a still-live kobject.
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `struct kobject *` with usage=1, so per-file
  scans on functions like `kobject_rename` get a live ghost and
  produce real verdicts (rather than empty-ghost low-confidence
  ones).
