# alloc_tag property module

Tracks per-pointer allocator tags across the Linux kernel to
catch allocator-mismatch bugs: `vfree(p)` where `p` came from
`kmalloc`, or `kfree(p)` where `p` came from `vmalloc`.

## Bug class

The kernel offers multiple allocator families with distinct
free paths:

| alloc         | valid free        |
|---------------|-------------------|
| `kmalloc`     | `kfree`, `kvfree` |
| `vmalloc`     | `vfree`, `kvfree` |
| `kvmalloc`    | `kvfree`, `vfree`, `kfree` (kvfree dispatches) |
| `kmem_cache_alloc` | `kmem_cache_free`, `kfree` |
| `alloc_pages` | `free_pages`, `__free_pages` |

Mismatching the alloc/free pair corrupts the heap:

- `vfree(p)` where `p = kmalloc(...)` walks the vmalloc page
  table for a pointer that is not in it.
- `kfree(p)` where `p = vmalloc(...)` treats the per-page
  allocation as a slab object — subsequent slab-free metadata
  writes corrupt the vmalloc header.

Both have appeared as real CVE-class regressions in the kernel
history.

## Files

- [`alloc_tag.h`](alloc_tag.h) — public API: `alloc_tag_t`
  enum, ghost-state mark/clear/read, and `alloc_tag_kfree_ok`
  / `alloc_tag_vfree_ok` predicates.
- [`alloc_tag.c`](alloc_tag.c) — reference implementation of
  the per-pointer ghost tag table.
- [`test_unit.c`](test_unit.c) — six-case unit test covering
  UNKNOWN, KMALLOC, VMALLOC, KVMALLOC, clear, and NULL.
- [`alloc_tag.cocci`](alloc_tag.cocci) — Coccinelle prefilter
  flagging `vfree` and `kvfree` call sites (the `kfree` path
  is too noisy to flag every site; the modelling on mismatched
  `vfree` is the primary anchor).
- [`run.sh`](run.sh) — regression runner.

## Scan integration

Adapter: [`../../scan/adapters/alloc_tag_kernel_adapter.c`](../../scan/adapters/alloc_tag_kernel_adapter.c)
attaches `alloc_tag_vfree_ok(p) == 1` as a contract
precondition on `vfree`.  The direct-call harness under
[`../../scan/adapters/alloc_tag_kernel_direct_harness.c`](../../scan/adapters/alloc_tag_kernel_direct_harness.c)
tags a pointer as `ALLOC_TAG_KMALLOC` (vulnerable) or
`ALLOC_TAG_VMALLOC` (fix direction) and calls `vfree`, making
the mismatch fire the contract.

## Why a new semantic class?

The first five property modules (aead, pipe_buffer,
cred_lifetime, lock_state, refcount_lifetime) all track either
a refcount or a structural-invariant bit, with state modelled
as "held/not-held" or "positive/zero".  alloc_tag tracks a
per-pointer discrete TAG (enum), which requires the ghost
table to store a different kind of value and the predicates
to compare against a set of legal tags rather than a single
threshold.  This validates that the scan infrastructure
generalises beyond the ghost-counter pattern — the template
accommodates tags, and future modules using the same shape
(e.g. resource-ownership state machines) can reuse the
scaffolding.
