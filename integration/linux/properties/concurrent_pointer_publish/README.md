# concurrent_pointer_publish property module

The first property module in the catalog using **CBMC's
concurrent execution model**.  Targets a major class of
kernel race bugs: producer threads that publish a "ready" /
"valid" flag before the data the flag advertises is
initialised, exposing concurrent readers to uninit / stale
state.

## Bug class

```c
// Writer thread:
shared->ready = 1;     // publish (BUG: too early)
shared->data  = 42;    // initialise after

// Reader thread:
if (shared->ready) {
    int v = shared->data;   // may read 0 — uninitialised
}
```

The standard kernel fix is to flip the order (init then
publish) and use `smp_store_release()` /
`smp_load_acquire()`, or to use `rcu_assign_pointer()` for
pointer publication.

Motivating CVE class: `race_or_toctoue` from the 2023-2026
kernel CVE survey — **232 CVEs (2.7% of classified
volume)**.  Concrete recent examples in this shape:
CVE-2026-43420 (ceph i_nlink underrun during async unlink),
CVE-2026-43439 (cgroup race between task migration and
iteration), and the AF_VSOCK / Bluetooth / netfilter race
families.

## Ghost shape

This is the catalog's **fifth distinct ghost shape**:
**two integer global flags** `__cpp_published` and
`__cpp_initialised`.  CBMC's concurrent symex explores all
interleavings of writes from the writer thread and
checks from the reader thread.

| Shape | Modules using it |
|---|---|
| Per-pointer balance     | cred / kobject / refcount / device / of_node / inode / dentry / fput / sock / skb / module / kref |
| Per-pointer flag        | page_provenance / pipe_buffer / lock_state / alloc_tag / cancel_work_before_free |
| Single global counter   | rcu_critical_section |
| Per-pointer integer     | netlink_attr_validation |
| **Two integer globals + concurrent execution** | **concurrent_pointer_publish** |

## Why integers, not pointers?

CBMC's pointer-concurrency model is unsound.  CBMC warns
`"pointer handling for concurrency is unsound"` and refuses
to prove anything when shared *pointers* are involved.

We sidestep this by modelling the published / initialised
state with two integer flags rather than the actual
shared pointer.  The bug shape we want to surface is a
**write-ordering question** — whether `published` becomes 1
before `initialised` does — and that question is faithful
regardless of whether the underlying state is a pointer or
an integer.

The cocci prefilter does flag pointer-publish call sites in
real kernel source (`x->p = new` followed by `new->y = ...`
without an intervening `smp_store_release` or
`rcu_assign_pointer`); the synthetic harness then validates
the ABSTRACT bug shape, not the literal pointer.

## Files

- [`concurrent_pointer_publish.h`](concurrent_pointer_publish.h)
  — public ghost API.
- [`concurrent_pointer_publish.c`](concurrent_pointer_publish.c)
  — reference impl (two int globals + helpers).
- [`test_unit.c`](test_unit.c) — sequential sanity tests.
- [`concurrent_pointer_publish.cocci`](concurrent_pointer_publish.cocci)
  — Coccinelle prefilter for "ready/valid/active field set
  before sibling data fields" patterns.
- [`run.sh`](run.sh) — regression runner.

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/concurrent_pointer_publish_kernel_adapter.c`](../../scan/adapters/concurrent_pointer_publish_kernel_adapter.c)
  contracts a synthetic `__cpp_assert_safe_to_read`
  checkpoint with precondition `cpp_safe_to_read() == 1`.
- Direct-call harness:
  [`../../scan/adapters/concurrent_pointer_publish_kernel_direct_harness.c`](../../scan/adapters/concurrent_pointer_publish_kernel_direct_harness.c)
  uses CBMC's `__CPROVER_ASYNC_1` label to spawn a writer
  thread; main runs as the reader.  Vuln shape (publish
  before init) → CBMC explores interleavings, finds one
  where the reader sees published=1 with initialised=0,
  fires the contract.  Fix shape (init before publish) →
  no such interleaving; verification clean.

CBMC's per-scenario time on this harness is **~20 ms** for
both vuln and fix directions — concurrency is fast at this
scale.  Per-file synthesis support is intentionally NOT
provided — concurrent reasoning over arbitrary kernel
functions doesn't fit the per-file model directly.

## What this module does NOT cover

* **Real pointer publication** as such.  Cocci flags it; the
  CBMC verification runs on the abstract integer-flag model.
  Catching the exact pointer race in the kernel TU would
  require CBMC's pointer-concurrency unsoundness fix or a
  goto-instrument pass that translates pointer publishes to
  integer-flag publishes.

* **Memory-ordering models beyond sequential consistency**.
  CBMC defaults to sequential consistency; weak memory
  (ARM/POWER) bugs that need `--mm tso` etc. are out of
  scope.  Most kernel race fixes assume sequential
  consistency under the appropriate barriers anyway.

* **Multi-thread (>2) cross-CPU races**.  The harness has
  one writer thread and one reader; the bug class targeted
  is fundamentally pairwise.

* **Per-file synthesis** — the per-file harness can't easily
  model concurrent execution.  Cocci is the primary
  integration path for real kernel source; CBMC validates
  the abstract bug shape on the synthetic harness.

## Honest limitations

This module is the catalog's first concurrency consumer.  Two
specific limitations to keep in mind:

1. The two-thread harness is **schematic, not exhaustive**.
   It demonstrates ONE specific race shape (publish-before-
   init).  Other concurrency bug classes — TOCTOU,
   double-locking, lock-ordering inversions — are outside
   this module's scope.

2. **CBMC concurrency scales worse than sequential.**
   Per-scan budget on this module's direct-call harness is
   ~20 ms, but a more complex kernel function could push
   into seconds or minutes.  Per-file synthesis would need
   benchmarking before it can be added.
