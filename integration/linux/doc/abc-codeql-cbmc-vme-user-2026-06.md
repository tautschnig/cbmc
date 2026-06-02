# A+B+C + CodeQL→CBMC, end-to-end on real kernel code: vme_user

**Date:** 2026-06-02
**Status:** First end-to-end run of the combined pipeline on a
real kernel driver.  Produced one **plausible** OOB candidate
with a concrete CBMC witness and filtered a false positive.
The candidate is **not** a confirmed bug (see caveats).

## What was run

* **Tree (A/C):** `drivers/staging/vme_user` — staging
  (less-swept) char-device driver.  Originally targeted
  `v7.1-rc5` (best A) but the CodeQL 2.25.5 C frontend hit its
  1000-error parse limit on `vme_user.c`/`vme.c` at that
  bleeding-edge revision (an honest tooling limit: industrial
  CodeQL lags the freshest kernel syntax).  Pivoted to the
  **6.12 LTS** tree, which extracts cleanly.
* **Stage 1 (CodeQL, over-approx):** `tainted_copy_size.ql` —
  taint from a user length parameter (`count`/`len`/`size`) to
  a `copy_from_user`/`copy_to_user`/`memcpy` size argument.
* **Stage 2 (CBMC, refute):** `vme_user_refine.c` — models the
  copy paths, havocs the window/length, runs `--bounds-check`.

Build recipe (lean, no sanitizer instrumentation — KASAN/
UBSAN/KCOV bloat broke extraction under allmodconfig):
`make defconfig` + `CONFIG_STAGING/VME_BUS/VME_USER` +
`modules_prepare`, then `CCACHE_DISABLE=1 codeql database
create --command="make -j8 drivers/staging/vme_user/"`.
(ccache must be disabled or the extractor sees cache hits and
records nothing.)

## Stage-1 result (3 candidates)

```
resource_from_user (vme_user.c:147)  copy_from_user size <- count
buffer_from_user   (vme_user.c:172)  copy_from_user size <- count
buffer_to_user     (vme_user.c:160)  copy_to_user   size <- count
```

## Stage-2 result (refinement)

| candidate | CBMC verdict | meaning |
|---|---|---|
| `resource_from_user` | **SUCCESSFUL** | `count` is clamped to `size_buf` (== `kern_buf` size); **false positive, filtered** |
| `buffer_from_user` | **FAILED**, witness `image_size=46, ppos=44, count=2` | writes `kern_buf[44..46)` in a buffer of size `BUF` (scaled `PCI_BUF_SIZE`); **OOB** |

`buffer_to_user` is the symmetric read-side analog of
`buffer_from_user` (same window-vs-buffer bound, `copy_to_user`
out of `kern_buf`), so it carries the same obligation.

## Root cause (the refinement question, answered)

`kern_buf` is a **fixed `PCI_BUF_SIZE` (0x20000 = 128 KiB)**
allocation (`vme_user.c:580,609`).  Two copy paths:

* **MASTER** (`resource_*`): clamps `count` to
  `size_buf == PCI_BUF_SIZE` before copying — safe.
* **SLAVE** (`buffer_*`): bounds `count` by
  `image_size = vme_get_size(resource)` — the VME *window*
  size — and copies to `kern_buf + *ppos`.  It never clamps to
  `size_buf`.

`image_size` is set by the `VME_SET_SLAVE` ioctl from
user-supplied `slave.size`, which `vme_slave_set` validates
only via `vme_check_window` (against the **VME address space**,
which for A32 is up to 4 GiB) — **not** against `PCI_BUF_SIZE`.
So a user who configures a slave window larger than 128 KiB
makes `image_size > size_buf`, and `buffer_from_user` /
`buffer_to_user` read/write past the fixed `kern_buf`.

CBMC confirms the obligation precisely: with the window
(`image_size`) allowed to exceed the buffer, it finds a
concrete `(image_size, ppos, count)` driving the copy out of
bounds; when the clamp is to `size_buf` (MASTER path) it
proves safety.  This is exactly the over-approximate-then-
refute split working on real code: CodeQL flagged all three;
CBMC kept the genuinely-unclamped ones and discarded the
clamped one.

## Honest caveats — why this is a CANDIDATE, not a confirmed bug

1. **Staging.** `drivers/staging/vme_user` is staging code,
   held to a lower bar; such "the driver assumes the window
   fits the buffer" gaps may be known/tolerated.
2. **Privilege + hardware.** `/dev/vme_user*` access is
   privileged and requires a VME bridge (real `tsi148`/`ca91cx42`
   or the `vme_fake` bridge).  The attack surface is narrow.
3. **Scaled model.** CBMC ran on a faithfully-structured but
   **scaled-down** model (`BUF=8`, `WIN_MAX=64`), not the real
   `vme_user.o`.  The structure (fixed buffer vs window-bounded
   copy) is preserved, but this is not a whole-driver proof.
4. **Not dynamically reproduced.** No KASAN/runtime PoC against
   the fake bridge was run.
5. **Not checked against the CVE record / maintainers.** I have
   not confirmed this is undisclosed.

So: a **plausible, refinement-confirmed conditional OOB** in
staging, surfaced by the combined pipeline — worth a
maintainer / KASAN check, **not** a filed finding.

## Why the run still matters

Independent of whether `vme_user` is a real, novel bug, this is
the first time the full strategy ran end-to-end on real kernel
code and behaved exactly as designed:

* CodeQL (over-approx) turned the driver into 3 tainted-size
  candidates with high recall;
* CBMC (precise) **separated** them — a concrete OOB witness
  for the unclamped SLAVE path, a safety **proof** for the
  clamped MASTER path (FP filtered);
* the divide-of-labour that the retrospective predicted
  (reachability = CodeQL, arithmetic/bounds feasibility = CBMC)
  held in practice.

It also surfaced two real operational lessons (recorded for
reuse): **(a)** disable ccache and **(b)** use a lean config
without KASAN/UBSAN/KCOV when building a CodeQL kernel DB; and
**(c)** CodeQL's frontend can fail to parse a bleeding-edge
kernel revision, so pin to a recent-but-not-rc tree.

## Artifacts

* `tainted_copy_size.ql` — stage-1 query.
* `vme_user_refine.c` — stage-2 CBMC harnesses.
* DB build: lean `defconfig` + VME, `CCACHE_DISABLE=1`.

## Next steps

1. **Maintainer / KASAN confirmation** of the `vme_user` window
   vs `PCI_BUF_SIZE` gap before any report; check the CVE
   record for prior disclosure.
2. **Auto-harnessing**: generate the stage-2 CBMC harness from
   the CodeQL candidate (source + enclosing guards) instead of
   hand-modelling — the remaining engineering to make the
   pipeline push-button.
3. **Scale stage-1** to a kernel-source taint model (mark
   `copy_from_user`/`nla_get_*`/ioctl args as sources globally)
   and run over a whole less-swept subsystem.
