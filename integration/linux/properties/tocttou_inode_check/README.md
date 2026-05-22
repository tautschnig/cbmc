# tocttou_inode_check property module

The catalog's third concurrency module.  Targets the **TOCTOU
subset** of the `race_or_toctoue` CVE bucket (232 CVEs total
in the 2023-2026 survey).

## Bug class

A thread reads a piece of shared state, decides based on the
read, and acts.  A concurrent thread mutates the state
between the read and the act, breaking the assumption the
act was predicated on:

```c
// Thread A (the buggy checker):
if (inode->i_size > MAX) {
    // ... possible interleave: another thread truncates ...
    do_something_with(inode->i_size);   // now smaller than MAX
}

// Thread B (concurrent mutator):
truncate(inode);
```

Concrete recent CVEs in this shape:
- CVE-2026-43420 (ceph i_nlink underrun during async unlink)
- CVE-2026-43439 (cgroup race between task migration and
  iteration)

## Ghost shape

Single integer `__tic_state` modelling the shared state.
Three contracts:

| Contract | Effect |
|---|---|
| `__tic_check`  | Pure read; ensures `return_value == __tic_state`. |
| `__tic_act`    | Requires `__tic_state == checked` (captured value still matches). |
| `__tic_change` | Mutator; assigns `__tic_state` and ensures it equals the new value. |

Same single-counter ghost pattern as
`concurrent_pointer_publish` and `concurrent_double_put`,
applied to a generic state field.

## Files

- [`tocttou_inode_check.h`](tocttou_inode_check.h) — public API.
- [`tocttou_inode_check.c`](tocttou_inode_check.c) — reference impl.
- [`test_unit.c`](test_unit.c) — sequential sanity tests.
- [`tocttou_inode_check.cocci`](tocttou_inode_check.cocci) —
  Coccinelle prefilter for "if (x->f) {... x->f ...}" patterns
  where the check and use are on the same field.
- [`run.sh`](run.sh) — regression runner.

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/tocttou_inode_check_kernel_adapter.c`](../../scan/adapters/tocttou_inode_check_kernel_adapter.c)
  contracts the three primitives.
- Direct-call harness:
  [`../../scan/adapters/tocttou_inode_check_kernel_direct_harness.c`](../../scan/adapters/tocttou_inode_check_kernel_direct_harness.c)
  spawns a mutator thread; main runs as the checker.  The
  vuln shape calls check-then-act with no lock; CBMC finds
  an interleaving where the mutator runs in between.  The
  fix shape wraps check-and-act in
  `__CPROVER_atomic_begin/_end` to model a lock held across
  the sequence; the mutator either ran entirely before or
  entirely after.

Per-scenario CBMC time: ~21 ms each.

## What this module does NOT cover

* **Per-file synthesis** on real kernel functions —
  concurrent reasoning doesn't fit per-file directly.  The
  cocci prefilter is the primary integration path.

* **Memory-ordering models beyond sequential consistency.**

* **Multi-thread (>2) cross-CPU TOCTOU scenarios.**

## Honest limitations

The harness models a single integer state field.  Real
kernel TOCTOU bugs span multi-field structures (e.g.
`inode->i_state` is a bitfield with multiple flag bits),
locks held inconsistently, and check/act sequences that span
multiple functions.  The module captures the abstract
pattern; identifying real candidates requires the cocci
prefilter plus hand triage.
