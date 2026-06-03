# hmm_store reachability: RESOLVED — user-reachable with oversized bytes

**Date:** 2026-06-03
**Question (from triage):** is `hmm_store`'s missing bounds check
(`hmm_check_bo` never bounds `bytes` against the BO size)
reachable from an atomisp ioctl with an attacker-controlled,
oversized `bytes`?

**Answer: YES.** A concrete user-reachable path exists where
`bytes` is a user field that is *not* cross-checked against the
destination buffer-object size.

## The path

```
ioctl(/dev/video*, ATOMISP_IOC_S_ISP_FPN_TABLE, struct v4l2_framebuffer *arg)
  atomisp_ioctl.c:1518   atomisp_fixed_pattern_table(asd, arg)
  atomisp_cmd.c:3375     atomisp_v4l2_framebuffer_to_css_frame(arg, &raw_black_frame)
  atomisp_cmd.c:3332       ia_css_frame_allocate(&res, arg->fmt.width,
                                                 arg->fmt.height, sh_format,
                                                 padded_width, 0)   // BO sized by W×H
  atomisp_cmd.c:3339       tmp_buf = vmalloc(arg->fmt.sizeimage)
  atomisp_cmd.c:3344       copy_from_user(tmp_buf, arg->base, arg->fmt.sizeimage)
  atomisp_cmd.c:3349       hmm_store(res->data, tmp_buf, arg->fmt.sizeimage)  // copies sizeimage
        hmm.c:374            hmm_store(virt=res->data, data=tmp_buf, bytes=sizeimage)
        hmm.c:393            hmm_check_bo(bo, virt)   // NO bytes-vs-size check
        hmm.c:401/411/444    memcpy(dst, data, bytes) / bo->pages[idx] walk
```

## Why it overflows

`res->data` (the destination BO) is sized by
`ia_css_frame_allocate(width, height, format, padded_width)` —
i.e. by `arg->fmt.width × arg->fmt.height × bpp`.  But
`hmm_store` copies `arg->fmt.sizeimage` bytes.  `width`,
`height`, and `sizeimage` are **independent, user-controlled
fields** of the same `struct v4l2_framebuffer`; nothing in the
chain checks `sizeimage ≤ frame_size`.

Attacker recipe: set `width = height = small` (tiny BO) and
`sizeimage = large`.  `hmm_store` then walks `bo->pages[idx]`
with `idx = (virt − bo->start) >> PAGE_SHIFT` past the BO's
`pages[]` array and `memcpy`s into whatever those out-of-range
page pointers reference — a kernel heap/BO overflow.

## CodeQL status (honest)

The intended automated confirmation — a path query from the
ioctl `arg` to `hmm_store`'s `bytes` argument
(`atomisp_sizeimage_path.ql`) — returned **0 paths**.  Root cause
is a **database-extraction gap, not absence of the flow**:
`atomisp_cmd.c` (which holds both the call site and
`atomisp_v4l2_framebuffer_to_css_frame`) was **not extracted**
into the allmodconfig `staging-db` — a probe with `hmm_store_sites.ql`
finds 20 `hmm_store` calls (all in firmware/runtime TUs) but not
the `atomisp_cmd.c:3349` site, and `atomisp_v4l2_framebuffer_to_css_frame`
is absent while only the *header* declaration of
`atomisp_fixed_pattern_table` is present.  The `.o` was almost
certainly served from a build cache, so the extractor never saw
the source.

To reproduce the automated path: rebuild the CodeQL DB with the
atomisp objects cleaned first (`find drivers/staging/media/atomisp
-name '*.o' -delete`) so `atomisp_cmd.c` is freshly compiled and
captured, then re-run `atomisp_sizeimage_path.ql`.  The manual
call-graph trace above is the authoritative evidence and does not
depend on that rebuild.

## Classification

`hmm_store` → **reachable-with-oversized-bytes (likely real OOB)**,
upgraded from "uncertain".  Caveats kept honest:
* atomisp is rough staging and binds only on matching Intel ISP
  PCI IDs; exploitability needs the device present and
  `/dev/video*` access (typically the camera group).
* Not dynamically confirmed (no atomisp HW in the QEMU setup).
* This is a *separate* finding from the vme_user OOB and was
  surfaced by the same CodeQL→CBMC staging sweep.

Recommended next action: treat as a candidate kernel bug report
for `drivers/staging/media/atomisp`; the minimal fix is to bound
`sizeimage` against the allocated frame size in
`atomisp_v4l2_framebuffer_to_css_frame` (and/or add a
`virt + bytes ≤ bo->start + bo->size` check in `hmm_check_bo`).
