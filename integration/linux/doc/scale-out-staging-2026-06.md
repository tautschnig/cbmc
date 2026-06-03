# Scale-out: CodeQL→CBMC pipeline over drivers/staging (linux_6_12)
# Scale-out: CodeQL→CBMC pipeline over drivers/staging (linux_6_12)

> **⚠️ CORRECTION (2026-06-03, end of day).** The numbers in the
> body below (27 candidates) were produced on a CodeQL DB that
> **silently under-extracted ~30% of staging TUs** — CodeQL's EDG
> frontend can't parse the `__seg_gs` named-address-space qualifier
> in `current.h`, poisoning body extraction for a config-dependent
> set of files (rtl8723bs, atomisp, vt665x, octeon, even
> `vme_user.c` itself).  Root cause + fix:
> `codeql-extractor-coverage-2026-06.md`.
>
> After rebuilding with the `percpu.h` fix (body coverage 329 → 455
> of 470 processed TUs), the **corrected** sweep finds:
>
> | metric | seg-poisoned DB | corrected (`-noseg`) DB |
> |--------|-----------------|--------------------------|
> | staging candidates | 27 | **58** |
> | CBMC SUCCESSFUL (proven safe) | 6* | **11** |
> | CBMC FAILED | 21* | **47** |
> | NEW candidates surfaced | — | **31** |
>
> \* the "6/21" reflect the post-harnesser-extension run; the
> original body of this doc predates those extensions.  The
> authoritative current figures are **58 / 11 / 47**.  Per-candidate
> verdicts: `scan/abc-refinement/poc/staging_verdicts_noseg.tsv`.
> Triage of the 47 FAILED: `staging-triage-failed-2026-06.md`.
>
> The 31 NEW candidates cluster in the previously-invisible drivers:
> **rtl8723bs/rtl8192e wifi** (IE/beacon/WPS memcpys), **av7110
> media** (DEBI/DVB), **gdm724x USB**, **atomisp**.  The vme_user
> findings are unaffected (found via the separate `vme-db`).
>
> Everything below this line is the original (pre-correction) text,
> retained for the record.
> ─────────────────────────────────────────────────────────────

**Date:** 2026-06-03
**Purpose:** Test the automated pipeline at scale over a broader
less-swept subsystem and measure yield / FP rate.

## Setup

* **Tree:** linux_6_12 (LTS), `allmodconfig` with sanitizers
  disabled (KASAN/UBSAN/KCOV/KFENCE off, WERROR off).
* **CodeQL DB:** built over all of `drivers/staging/`
  (50.87 MiB, ~150+ source files compiled).
* **Stage 1:** `tainted_copy_size_structured.ql` (parameter
  named count/len/size/n/nbytes of IntegralType → arg2 of
  copy_from_user/copy_to_user/memcpy/memmove).
* **Stage 2:** `auto_harness.py` → CBMC `--bounds-check`.

## Stage-1 results

**27 driver-level candidates** across 7 staging subsystems
(none in vme_user — that driver was scanned separately in
`/tmp/vme-db`; see `abc-codeql-cbmc-vme-user-2026-06.md`):

| Subsystem | Candidates |
|-----------|-----------|
| greybus | 8 |
| fieldbus/anybuss | 8 |
| media (av7110, atomisp, meson) | 6 |
| vc04_services (vchiq) | 2 |
| most | 1 |
| gdm724x | 1 |
| rts5208 | 1 |

## Stage-2 results (auto-harnesser)

### Baseline (fixed-buffer model, no allocation awareness)

| Verdict | Count | Interpretation |
|---------|-------|----------------|
| VERIFICATION FAILED | 27 | No in-function size_buf clamp → CBMC finds OOB in the fixed-buffer scaled model |
| VERIFICATION SUCCESSFUL | 0 | — |

The baseline harnesser models a FIXED buffer (`BUF=8`) and so
over-reports on every candidate whose buffer is actually sized
from the (user-controlled) length — the dominant FP pattern.

### After allocation-aware harnessing (see "Improvement" below)

| Verdict | Count | Interpretation |
|---------|-------|----------------|
| VERIFICATION FAILED | 23 | Fixed/unknown buffer, or copy into a caller-provided buffer — needs manual or interprocedural analysis |
| VERIFICATION SUCCESSFUL | 4 | Buffer provably sized ≥ copy length → safe by construction |

The 4 newly-proven-safe candidates (true FPs eliminated):

| Candidate | Why safe |
|-----------|----------|
| `gb_raw_send:136` (len) | `request = kmalloc(len + sizeof(*request))`, copy `len` into `&request->data[0]` |
| `receive_data:84` | `raw_data = kmalloc(struct_size(raw_data,data,len))`, copy `len` |
| `gb_loopback_operation_sync:385` | `gb_operation_create(…,request_size,…)` sizes payload; copy size == `request_size` |
| `gb_loopback_async_operation:486` | same framework-allocator pattern |

## Honest assessment of the 23 remaining FAILED candidates

Most are still **false positives at the pipeline level** — not
real OOB bugs — for reasons the current harnesser cannot yet
model:

**Copy INTO a caller-provided buffer** (need caller context):
* `gb_loopback_operation_sync:395` — `memcpy(response,
  operation->response->payload, response_size)`: the destination
  `response` is the caller's buffer; safety depends on the
  caller sizing it ≥ `response_size`.

**Interprocedural / struct-field sizing not yet modelled:**
* `gb_hid_set_report:117` — copies `len` into
  `operation->request->payload->report`, where the payload was
  sized `size`; relationship `len ≤ size − header` is real but
  not textually matched by the current sound check.
* `gb_spi_operation_create`, `gdm_mux_send`, `create_area_*`,
  `anybuss_*` — similar struct-embedded or helper-sized buffers.

**Confirmed real OOB:** none in this staging set yet (the
vme_user finding is from the separate `/tmp/vme-db` run, not
counted here).

**Uncertain — need deeper manual inspection** (see triage doc):
* `_anybus_mbox_cmd` (×3) — fixed `msg[]` field vs extended buf.
* `hmm_store` (×3) — ISP DMA buffer sized from HW config.
* `dvb_filter_pes2ts`, `write_ipack` — PES packet assemblers;
  `len` bounded by packet structure.
* `vchiq_ioc_copy_element_data` — bounded by `min(element->size,
  maxsize − copied)`.

## Yield metrics

| Metric | Baseline | After alloc-aware |
|--------|----------|-------------------|
| Stage-1 candidates (staging) | 27 | 27 |
| Stage-2 FAILED | 27 | 23 |
| Stage-2 SUCCESSFUL (proven safe) | 0 | 4 |
| True FPs eliminated by CBMC | 0 | 4 |
| Confirmed real OOB (this set) | 0 | 0 |
| Uncertain (need manual audit) | — | 7 (see triage doc) |

Note on precision: with zero confirmed real bugs in the staging
set so far, a precision figure is not yet meaningful here; the
honest statement is that allocation-aware modelling converted 4
candidates from "alarm" to "proven safe", shrinking the manual
triage burden from 27 to 23 (and the genuinely-uncertain subset
to 7).  The known real OOB lives in vme_user, scanned separately.

## Improvement: allocation-aware harnessing (2026-06-03)

`auto_harness.py` gained two sound allocation-aware paths:

1. **In-function proportional alloc** — if a buffer is allocated
   in the function with a size expression that references the
   tainted length, and that buffer is the copy destination, model
   it as `malloc(len)`.  CBMC then proves the copy in-bounds.
   Catches `gb_raw_send`, `receive_data`.

2. **Framework allocator** (`KNOWN_ALLOC_HELPERS`) — for helpers
   like `gb_operation_create(conn, type, request_size, …)` whose
   Nth argument sizes the payload, match the copy's *actual* size
   argument (extracted from the sink line) against the allocator's
   size argument.  Only fires when they are textually identical
   AND the alloc-target variable appears in the copy destination —
   a deliberately conservative (sound) check.  Verified that
   `gb_operation_create` arg[2] → `gb_operation_message_alloc` →
   `kzalloc(request_size + header)` genuinely sizes the payload.

Both paths are conservative: they only ever turn a FAILED into a
SUCCESSFUL when the buffer is provably ≥ the copy length, so they
cannot mask a real OOB.

## Lessons for pipeline improvement

1. **Allocation-aware harnessing** — done (above); eliminated 4
   of the staging FPs.  Extending `KNOWN_ALLOC_HELPERS` and the
   struct-field-sizing relationship would catch more.

2. **Caller-context propagation:** Guards/buffer sizes in the
   caller (e.g. vme_user_write's `image_size` clamp, or the
   `response` buffer in gb_loopback) aren't visible within the
   analysed function.  An interprocedural harnesser that models
   the caller's pre-conditions would produce tighter verdicts.

3. **CodeQL source specificity:** The current source predicate
   (any param named count/len/size) over-triggers on internal
   helpers.  Restricting to params that receive values from
   the VFS/ioctl/netlink entry points would improve precision.

4. **CBMC modular verification:** Instead of scaled models,
   running CBMC on the actual .o (goto-cc + harness insertion)
   would eliminate the modeling gap entirely — future work.

## Conclusion

The pipeline **works at scale** — it processes a full staging
subsystem (485 source files) end-to-end in ~5 minutes (2m41s
CodeQL + ~1s per CBMC harness).  Allocation-aware harnessing
turned 4 of the 27 staging candidates from "alarm" to
"proven safe" with a sound (conservative) check, shrinking the
manual triage set to 23 and the genuinely-uncertain subset to 7.
No confirmed real OOB has surfaced in the staging set yet; the
known real bug lives in vme_user (scanned separately).  Honest
takeaway: the remaining FPs are well-characterized (copy into a
caller-provided buffer, struct-field-sized payloads) and call
for caller-context / struct-aware modelling next.


## Update (2026-06-03): offset-bound harnesser extension

`auto_harness.py` gained a sound `detect_offset_bound` path
recognizing three guard idioms that all reduce to
"off + count <= buffer":

* subtractive clamp `count = LIMIT - off` (also fixes the *fixed*
  vme_user `buffer_from_user`, whose patched clamp the heuristic
  previously mis-modelled);
* additive gate `if (off + count < LIMIT) { copy }`
  (`write_ipack`);
* `min()` clamp `n = min(..., LIMIT - off)`
  (`vchiq_ioc_copy_element_data`).

Modelling `off + count <= BUF` is conservative — the real guarded
or clamped copy writes no further — so it cannot mask an OOB.

Staging re-run: **SUCCESSFUL 4 → 6, FAILED 23 → 21**.  Newly
proven safe: `vchiq_ioc_copy_element_data` (min clamp),
`write_ipack` (additive gate).  Crucially `hmm_store` (×3) and
the other genuinely-unanalysed candidates **remain FAILED** — the
extension does not touch them, confirming it only clears provably
safe copies.  Combined with manual triage, the remaining 21
FAILED are: 1 likely-real (hmm_store, now reachability-confirmed
separately), and the rest caller-context / struct-field-sized /
loop-modular FPs that need interprocedural or loop-summary
modelling.
