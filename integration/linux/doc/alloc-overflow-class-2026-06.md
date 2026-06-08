# Bug class: integer overflow in allocation size

**Date:** 2026-06-08
**Files:** `tainted_alloc_overflow.ql` (stage 1), `alloc_overflow.c`
(stage-2 CBMC harness / class validation).
**Goal:** add a bug class CBMC decides *exactly* — a user value
reaching an allocation-size arithmetic expression (`n*elem`,
`hdr+len`) that can wrap, yielding an undersized buffer a later
copy overflows.

## Why CBMC is the right oracle here

Unlike the size-match heuristic (which is about clamps), this is
pure machine arithmetic.  CBMC with `--unsigned-overflow-check`
decides whether the alloc-size expression can wrap, and
`--bounds-check`/`--pointer-check` then catch the resulting OOB.

Validation (`alloc_overflow.c`, scaled model `u32 bytes = n*ELEM;
buf = malloc(bytes); buf[(size_t)n*ELEM - 1] = 0`):

| harness | verdict | property |
|---------|---------|----------|
| `harness_alloc_overflow_BUG` | **FAILED** | `arithmetic overflow on unsigned *` + `pointer outside object bounds` |
| `harness_alloc_overflow_FIXED` (guard `n > UINT32_MAX/ELEM`) | **SUCCESSFUL** | — |

The FIXED guard mirrors the real kernel remedy
(`check_mul_overflow` / `kmalloc_array` / `array_size()`).

## Stage-1 query

`tainted_alloc_overflow.ql` flags a **raw** allocator
(`kmalloc_noprof`, `kzalloc_noprof`, `vmalloc_noprof`,
`__kvmalloc_node_noprof`, `_rtw_malloc`, `devm_k*alloc`, …) whose
size argument **contains a `*` or `+`** and into which a
user-named length flows.  It deliberately EXCLUDES the
overflow-checked helpers (`kmalloc_array_noprof`, `devm_kcalloc`,
`array_size`, `struct_size`) — those are the fix, not the bug.

> Note: linux 6.12 lowers `kmalloc(...)` to `kmalloc_noprof(...)`
> (alloc profiling).  The first version of this query used the
> source-level names and matched **nothing**; the DB only has the
> `_noprof` forms.  This is the same "names differ from source"
> lesson as the `__seg_gs` extractor gap — always confirm the
> callee names actually present in the DB.

## Result on the `-noseg` staging DB

**14 candidates** across octeon, greybus, atomisp, rtl8723bs.
Triaged (spot checks):

| candidate | verdict | why |
|-----------|---------|-----|
| `gb_raw_send:132` `kmalloc(len + sizeof(*request))` | FP | caller `raw_write` caps `count > MAX_PACKET_SIZE` |
| `make_histogram:71/75/79` `kvmalloc(length * sizeof)` | FP | callers pass constants `ISP_PMEM_DEPTH`/`SP_PMEM_DEPTH` |
| `gb_loopback_*` `kmalloc(len + …)` | FP (likely) | `len` from sysfs config, bounded |
| `rtw_cbuf_alloc:226` `_rtw_malloc(… size …)` | FP (likely) | init-time fixed `size` |
| `cvm_oct_fill_hw_memory` | FP (likely) | driver-internal element size |

No confirmed overflow in staging — the arithmetic-alloc *shape* is
common but the dangerous operand is caller-bounded or constant.
This matches the bug-shape-query finding: the shapes are right,
staging just lacks an unguarded instance.

## How this finds bugs elsewhere

The class is high-yield on attacker-facing parsers (netlink, USB
descriptor, filesystem) where a length field multiplies an element
size before allocation.  The pipeline:
1. `tainted_alloc_overflow.ql` → arithmetic-alloc candidates;
2. auto-generate a CBMC harness modelling `alloc_size_expr` vs the
   later use size, with `--unsigned-overflow-check`;
3. FAILED + a concrete wrapping `n` ⇒ report; FIXED ⇒ prove safe.

The validation harness here is the template for step 2.
