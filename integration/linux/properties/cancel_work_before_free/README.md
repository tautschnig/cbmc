# cancel_work_before_free property module

Tracks pending work on `struct work_struct` instances to catch
the cancel-before-free UAF bug class:

```c
struct foo *x = kmalloc(sizeof(*x), GFP_KERNEL);
INIT_WORK(&x->work, worker);
schedule_work(&x->work);
...
kfree(x);          // BUG: pending work runs against freed memory
```

The fix is `cancel_work_sync(&x->work)` (or
`cancel_delayed_work_sync` for `struct delayed_work`,
`del_timer_sync` for timers) before the kfree.

Motivating CVE class: 51 CVEs in the 2023-2026 kernel CVE
survey fall under `cleanup_ordering`; many more in
`use_after_free` trace back to this shape.

## Files

- [`cancel_work_before_free.h`](cancel_work_before_free.h) —
  per-`work_struct *` ghost flag API.
- [`cancel_work_before_free.c`](cancel_work_before_free.c) —
  reference impl using the same pointer-keyed table pattern as
  the balance modules.
- [`test_unit.c`](test_unit.c) — four-case ghost test.
- [`cancel_work_before_free.cocci`](cancel_work_before_free.cocci)
  — Coccinelle prefilter with two rules:
  1. `INIT_WORK` call sites (high recall).
  2. `INIT_WORK ... kfree` without intervening
     `cancel_work_sync` (high precision; this is the actual
     bug shape).
- [`run.sh`](run.sh) — regression runner.

## Scan integration

* **Cocci prefilter** is the primary integration path: the
  `BUG-SHAPE` rule directly identifies cancel-before-free
  patterns in real kernel source.
* **Kernel adapter**:
  [`../../scan/adapters/cancel_work_before_free_kernel_adapter.c`](../../scan/adapters/cancel_work_before_free_kernel_adapter.c)
  contracts a synthetic `__assert_no_pending_work(work)`
  checkpoint function with precondition
  `cancel_work_pending(work) == 0`.
* **Direct-call harness**:
  [`../../scan/adapters/cancel_work_before_free_kernel_direct_harness.c`](../../scan/adapters/cancel_work_before_free_kernel_direct_harness.c)
  calls `cancel_work_set_pending` (mirroring INIT_WORK), then
  the checkpoint function.  Vuln shape skips
  `cancel_work_clear_pending` and the precondition fires; fix
  shape clears it and verifies clean.

## What this module does NOT cover (yet)

* **Per-file synthesis on real kernel functions.**  Wiring a
  real-kernel `kfree(x)` to the `__assert_no_pending_work`
  checkpoint requires either:
  - Per-type contracts on the freeing function (one contract
    per struct type that contains a work_struct), or
  - A goto-instrument pass that auto-injects the checkpoint
    before each `kfree(x)` where `x`'s type contains a
    `work_struct` field.

  The first requires per-module config; the second requires
  new infrastructure.  Deferred to a follow-up.

* **`delayed_work` and `timer_list` variants.**  The same
  shape applies to `cancel_delayed_work_sync` (for
  `struct delayed_work`) and `del_timer_sync` (for
  `struct timer_list`).  These are sibling modules to add.

## Limitations honestly stated

This module's Cocci prefilter does the heavy lifting on real
kernel source.  CBMC validates a synthetic harness that
exercises the bug shape, but it does NOT yet verify real
kernel functions automatically — the kfree side of the
contract is not connected to actual `kfree` call sites.

This is recorded as a Phase-3 follow-up in
`integration/linux/doc/phase-2-rcu-cancel-2026-05.md`.
