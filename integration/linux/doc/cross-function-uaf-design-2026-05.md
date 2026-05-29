# Cross-function bug tracking: design and proof-of-concept

**Date:** 2026-05-29

## Motivation

The current catalog operates per-function: each scan
produces a verdict for one function in isolation, with
callees treated as no-bodies (or stubbed via contracts).
This is fast and tractable but misses bug patterns where
the unsafe interaction crosses function boundaries:

* **CVE-2023-54214 (l2cap UAF):** function A frees a
  packet `skb`; function B, invoked via a callback later
  in the same trace, dereferences it.  Neither function
  in isolation is buggy.
* **CVE-2025-21838 (usb_del_gadget):** `device_del()` can
  schedule work before `flush_work()` runs.  The bug is
  the temporal ordering of two API calls in the *caller*,
  not within either callee.
* General use-after-free spans where the free is in a
  cleanup path of a parent function and the use is in a
  child function called transitively.

These shapes are out of reach for the current
methodology and would account for a significant tail of
the kernel's reported UAFs.

## Design

### Scope

We focus on a single cross-function shape for the
proof-of-concept:

> **Callee-frees-parameter:** a function F passes pointer
> P to a callee C; C frees P (via `kfree`, `put_*`,
> `release_*`, etc.); F then dereferences P.

This is the simplest cross-function UAF pattern and
covers a substantial fraction of CVE patterns under
"use_after_free / cleanup_ordering" categories.

### Key insight

The existing `use_after_free_generic` ghost already
tracks whether a pointer has been freed.  We can extend
the **callee contract** to update the ghost: when CBMC
applies `--replace-call-with-contract` on `kfree(p)`, the
contract can `__CPROVER_assigns(...)` the ghost flag for
`p`.  Then any subsequent `__assert_not_freed(p)` in F
sees the freed state.

### Implementation phases

1. **Phase 1 (PoC, this doc):** synthetic test showing
   that a kfree inside a callee correctly propagates to
   the caller's ghost state when contracts are applied
   to the callee.  Validates the mechanism.

2. **Phase 2 (next iteration):** wire the
   `use_after_free_generic` cocci instrumentation to
   emit `__assert_not_freed(p)` markers AT CALL SITES
   where a parameter that may have been freed by a
   callee is dereferenced — not just at intra-function
   `p->fld` accesses.

3. **Phase 3 (later):** annotate well-known
   freeing-callee APIs (`kfree`, `kfree_skb`, `put_cred`,
   `dput`, etc.) with their freeing semantics so the
   contracts auto-apply without per-callee
   instrumentation.

## Proof-of-concept

`integration/linux/scan/tests/cross_function_uaf/` (this
sprint).

The PoC builds a synthetic two-function scenario:

```c
// Callee: frees its argument.
void callee_frees(void *p) {
    kfree(p);
}

// Caller: passes p to callee, then dereferences p.
int caller_uses_after_free(void *p) {
    callee_frees(p);
    return *(int *)p;  // BUG: use-after-free
}
```

With the existing `use_after_free_generic` adapter,
`__assert_not_freed(p)` is a contract that requires
`uaf_freed(p) == 0`.  The cocci instrumenter inserts
`__assert_not_freed` markers at each `*p` deref or
`p->field` access AND inserts `uaf_track_freed(p)` after
each `kfree(p)`.

For cross-function detection, we need:
1. The cocci instrumenter to ALSO insert
   `uaf_track_freed(p)` (in the *callee*) after `kfree`.
2. The caller's `__assert_not_freed(p)` checks see the
   freed state because the ghost table is global.

The existing infrastructure already does (1) implicitly
via cocci's free-track rule.  Step (2) works because the
ghost table is `static` in the property module's TU,
which is shared across the linked goto binary.

So the PoC validates that: **with cocci instrumentation
applied to BOTH the caller and callee TUs, cross-function
UAF is detected.**

## Limitations

* **Static analysis only.**  CBMC's symex unwinds the
  caller's body and inlines the callee.  Genuinely
  asynchronous patterns (work_struct fires after
  device_del returns) need the harness to model the
  asynchronous schedule, which is out of scope.
* **Function-pointer callees.**  When the callee is
  invoked via a function pointer (kernel idiom for
  callbacks), CBMC can't always resolve the target.
  Goto-instrument's `--remove-function-pointers` helps
  but isn't perfect.
* **Whole-program scaling.**  Linking many kernel TUs
  together is expensive.  The PoC runs on two functions;
  scaling to whole-subsystem is a future iteration.

## What this proof-of-concept demonstrates

A single hand-crafted two-function file shows that:
1. Cocci correctly instruments both functions.
2. The linked goto binary's ghost table is shared.
3. CBMC's symex sees the cross-function flow and flags
   the use-after-free.

This validates the methodology without committing to
the full whole-program analysis.

## Next steps after the PoC

1. Run on a real kernel CVE that exhibits cross-function
   UAF (e.g. CVE-2023-54214 or a substitute) — multi-TU
   link.
2. Add a `cross_function_uaf` corpus track to
   `cve_validate.py` for systematic measurement.
3. Document per-CVE limitations (function-pointer
   resolution, async work, etc.) as we encounter them.
