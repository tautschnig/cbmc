# Manual triage of the 7 "uncertain" staging candidates

**Date:** 2026-06-03
**Context:** After allocation-aware harnessing, 23 staging
candidates remained FAILED.  The scale-out doc flagged 7 as
genuinely uncertain (not obviously a caller-buffer or
proportional-alloc FP).  This is the manual source audit of
those 7 (9 sub-candidates).

## Verdicts

| # | Candidate | Verdict | Reason |
|---|-----------|---------|--------|
| 1 | `_anybus_mbox_cmd:840` (ext_sz) | **FP / safe** | guard `if (ext_sz > sizeof(h->extended)) return -EINVAL;`, `extended[8]` (__be16 = 16 B) |
| 2 | `_anybus_mbox_cmd:841` (msg_out_sz) | **FP / safe** | guard `msg_sz = max(in,out); if (msg_sz > MAX_MBOX_MSG_SZ) return;`, `msg[MAX_MBOX_MSG_SZ]` (0xFF) |
| 3 | `_anybus_mbox_cmd:854` (msg_in_sz) | **FP / safe** | read from `msg[255]` bounded by same guard; write to caller buf bounded by `msg_in_sz ≤ 255` |
| 4 | `hmm_store:401` | **UNCERTAIN** | `hmm_check_bo()` validates bo!=NULL / pages / vaddr, but does **not** bound `bytes` against BO size |
| 5 | `hmm_store:411` | **UNCERTAIN** | same — `memcpy(vptr, data, bytes)` after vmap, no size check |
| 6 | `hmm_store:444` | **UNCERTAIN** | per-iteration `len` is page-bounded, but `bo->pages[idx]` index walks unbounded if `bytes` exceeds BO size |
| 7 | `dvb_filter_pes2ts:113` | **FP / safe** | `buf[188]`; `while (len >= 184)` reduces `len`∈[0,183]; `5 + (183−len) + len = 188` lands exactly at end |
| 8 | `write_ipack:126` | **FP / safe** | explicit guard `if (p->count + count < p->size)` around the memcpy |
| 9 | `vchiq_ioc_copy_element_data:93` | **FP / safe** | `bytes_this_round = min(element->size − offset, maxsize − copied)`; loop bounded by `maxsize` (the `dest` size) |

## Summary

* **6 of 7** function entries are **confirmed false positives**
  (provably safe): `_anybus_mbox_cmd` (all 3 sub-candidates),
  `dvb_filter_pes2ts`, `write_ipack`, `vchiq_ioc_copy_element_data`.
* **1 of 7** is **genuinely uncertain**: `hmm_store` (×3).

## The one real concern: hmm_store

`hmm_store(ia_css_ptr virt, const void *data, unsigned int bytes)`
copies `bytes` into an ISP buffer object located at `virt`.  The
only validation is `hmm_check_bo(bo, virt)`, which checks the BO
exists and has pages/vaddr — but **never checks
`virt + bytes ≤ bo->start + bo->size`**.  If a caller supplies a
`bytes` larger than the BO's remaining space, all three memcpy
sites (and the `bo->pages[idx]` walk at :444) overrun the object.

Caveats that keep this *uncertain* rather than *confirmed*:
* `data` is a kernel pointer, not `__user` — `hmm_store` is an
  internal ISP memory helper, not a direct syscall sink.  The
  taint reached `bytes` from a param named `size` several frames
  up; whether an attacker can drive an oversized `bytes` to this
  call with a mismatched `virt`/BO needs a full atomisp ioctl →
  firmware-load path trace, which is out of scope here.
* atomisp is a notoriously rough staging driver; a missing
  bounds check here is plausible but unproven as user-reachable.

Recommendation: flag `hmm_store` for a focused interprocedural
follow-up (CodeQL path query from atomisp ioctls to `bytes`), not
as a confirmed bug.

## Implications for the auto-harnesser

5 of the 6 FPs are caught by guard/clamp idioms the current
extractor misses.  These motivate concrete, *sound* extensions:

| FP | Missed idiom | Harnesser extension |
|----|--------------|---------------------|
| `_anybus_mbox_cmd` | intermediate `msg_sz = max(a,b); if (msg_sz > K) return` | track simple alias/`max()` into the guarded var |
| `write_ipack` | additive `if (base + count < size)` | recognize additive bound on the tainted param |
| `vchiq_*` | `n = min(cap − off, maxsize − done)` | recognize `min()` clamp to a buffer-derived cap |
| `dvb_filter_pes2ts` | loop-modular reduction (`while (len>=K) len-=K`) | hard; loop-summary needed |

Each is a precision win that stays sound (only ever turns a
FAILED into SUCCESSFUL when the guard provably bounds the copy).
