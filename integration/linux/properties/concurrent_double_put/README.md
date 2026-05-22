# concurrent_double_put property module

The catalog's second concurrency module.  Targets the
**refcount-race subset** of the `race_or_toctoue` CVE bucket
(232 CVEs total in the 2023-2026 survey).

## Bug class

Two threads each believe they hold a reference to a shared
object and concurrently call its put-API:

```c
// Thread A:                  // Thread B:
if (cred_live(c)) {            if (cred_live(c)) {
    put_cred(c);                   put_cred(c);     // BUG
}                              }
```

Without atomic check-and-decrement (e.g.
`refcount_dec_and_test`), both reads of the live state see
"still live" before either decrement runs; one ordering of
the puts ends up calling put on an already-zero refcount,
freeing memory the other thread still holds.

Concrete recent CVEs in this shape:
- CVE-2026-43322 (Bluetooth `le_read_features_complete` UAF)
- AF_VSOCK / sock_put races

## Ghost shape

Single integer `__cdp_live` (1 = live, 0 = dead).
Contracts mutate the ghost via `__CPROVER_assigns +
__CPROVER_ensures` so CBMC's concurrent symex sees a
sequentially-consistent decrement when one thread's put
completes.

This is the **same pattern** as `rcu_critical_section`'s
single-counter ghost, applied to a binary live-state.

## Files

- [`concurrent_double_put.h`](concurrent_double_put.h) —
  public ghost + helpers.
- [`concurrent_double_put.c`](concurrent_double_put.c) —
  reference impl.
- [`test_unit.c`](test_unit.c) — sequential sanity tests.
- [`concurrent_double_put.cocci`](concurrent_double_put.cocci)
  — Coccinelle prefilter for "if-live-then-put" patterns.
- [`run.sh`](run.sh) — regression runner.

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/concurrent_double_put_kernel_adapter.c`](../../scan/adapters/concurrent_double_put_kernel_adapter.c)
  contracts `__cdp_get` (assigns + ensures live=1) and
  `__cdp_put` (requires live==1; assigns + ensures live=0).
- Direct-call harness:
  [`../../scan/adapters/concurrent_double_put_kernel_direct_harness.c`](../../scan/adapters/concurrent_double_put_kernel_direct_harness.c)
  uses `__CPROVER_ASYNC_1` to spawn one thread; main runs
  as the second.  Each thread does
  `if (cdp_is_live()) __cdp_put()`.  CBMC's concurrent
  symex finds interleavings where both threads pass the
  check before either's put runs; the second put's
  precondition fires.  Fix shape uses
  `__CPROVER_atomic_begin/_end` to make the check-and-put
  indivisible.

Per-scenario CBMC time: ~25 ms each direction.

## What this module does NOT cover

* **Atomic-refcount races on `refcount_t`** — these are
  caught (sequentially) by the existing `refcount_lifetime`
  module.  This module specifically targets non-atomic
  state and check-then-act patterns.

* **Per-file synthesis** on real kernel functions —
  concurrent reasoning doesn't fit per-file directly.  The
  cocci prefilter is the primary integration path for
  surfacing real-kernel candidates.

* **Memory-ordering models beyond sequential consistency**
  — same caveat as concurrent_pointer_publish.

## Honest limitations

* The harness models a binary live flag (`__cdp_live = 1` or
  `0`), not a refcount.  Real kernel races involve refcounts
  going from 2 to 1 to 0; the bug shape is faithful for the
  decrement-from-1 case but the module doesn't catch races
  on intermediate refcount values.
* Two-thread harness only.
