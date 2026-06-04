# Sharpened "bug-shape" stage-1 query (fixed dest + unclamped user size)

**Date:** 2026-06-04
**Query:** `tainted_into_fixed_dest.ql`
**Goal:** raise precision so a candidate *means* something — target
the empirical bug shape (fixed-size destination + user-controlled
length not clamped to that size), not generic taint.

## Design

Flags a tainted length reaching the size arg of
`copy_from_user`/`memcpy`/`memmove` where:

* the **destination is itself a fixed-size array** (`arg0`'s type,
  after array-to-pointer decay, is an `ArrayType` with a known
  size) — *not* a child indexing array.  This excludes the
  dominant FP class (proportional allocation, whose dest is a
  `malloc`'d pointer), and avoids matching `kbuf[type]` /
  `image[minor].field` indexing arrays; and
* the length is **not clamped to a compile-time constant**
  (`Literal` / `sizeof` / enum) anywhere in the enclosing function.

## Result on the `-noseg` staging DB

| query | candidates |
|-------|-----------|
| broad `tainted_copy_size_structured.ql` | 58 |
| sharpened `tainted_into_fixed_dest.ql` | **6** |

~10× noise reduction.  All 6 are genuine "fixed array + tainted
size" shapes (worth a human look); on inspection all 6 are safe,
each via a guard **outside the query's intraprocedural view**:

| candidate | dest | why safe (out-of-view guard) |
|-----------|------|------------------------------|
| `_anybus_mbox_cmd:840` | `extended[16]` | guard on derived `ext_sz > sizeof(extended)` |
| `_anybus_mbox_cmd:841` | `msg[255]` | guard on derived `msg_sz > MAX_MBOX_MSG_SZ` |
| `av7110_p2t_write:725` | `p->pes[188]` | size `rest = (length-c) % (TS_SIZE-4)` < 184 (modulo) |
| `create_area_writer:652` | `ap->buf[512]` | caller `anybuss_*` clamps via `min_t(.., MAX_DATA_AREA_SZ-off, ..)` |
| `create_area_user_writer:671` (×2) | `ap->buf[512]` | caller `anybuss_write_input`: `len = min_t(loff_t, MAX_DATA_AREA_SZ-*offset, size)` |

## Assessment

* **Precision of the candidate set improved ~10×** and every hit is
  a legitimate bug *shape* — exactly the vme_user/hmm_store form
  (`create_area_user_writer` is a `copy_from_user` straight into a
  fixed 512-byte buffer; only the caller's `min_t` saves it).
* **No new true positive in staging** — consistent with the full
  47-FAILED triage.  The bug shape is right; staging just doesn't
  contain an unguarded instance.
* The residual FPs all stem from guards the query can't see:
  derived-variable clamps, modulo bounds, and **caller-side
  clamps**.  The last is the most common and is precisely what
  the real-code / interprocedural harness (step 3) addresses.

## Why this matters for finding bugs

A 6-candidate set is hand-auditable in minutes; a 58- (or 47-
FAILED) set is not.  Run this query first on each new subsystem
DB; the survivors are the ones to push through CBMC + the
validation loop.  On an attacker-facing subsystem with a genuinely
unclamped fixed-buffer copy, this query isolates it directly.
