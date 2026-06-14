# skb/cursor (field-read-before-length-check) full pipeline (2026-06)

**Date:** 2026-06-14.  Taking the A1-mem skb/cursor shape through the same
sound pipeline as count/index.  Recall for this shape is already validated
(ground_truth_recall.sh: read_field_at_offset / decode_le16_at_cursor, 2/2
vuln/fixed pairs).

## Wide triage (wide_triage_skb.py, 535/536 leaf DBs, 5124 distinct candidates)

| triage class | count | % |
|--------------|------:|--:|
| LOCALLY-GUARDED | 2818 | 54% |
| FRONTIER | 2242 | 43% |
| HEADER-HELPER | 64 | 1% |

kind: skb->data read 3066 / bounded-cursor decode 2058.

Classification:
* **HEADER-HELPER** -- defined in an include/ header (skb_copy_to_linear_data,
  skb_header_pointer, ...): a generic accessor whose length check is the
  caller's job (analogous to count/index bareparam).
* **LOCALLY-GUARDED** -- adv_lenguard=GUARDED (in-function pskb_may_pull /
  skb->len check), adv_trivial=yes (*_hdr accessor), or **adv_lenprop=LENPROP**
  (new).
* **FRONTIER** -- UNGUARDED, non-trivial, no lenprop, in a subsystem .c.

## Deep-dive -> the adv_lenprop FP-reducer

Sampling the frontier (mad.c, fw.c, wmi.c, xattr.c) showed the dominant FP
class is the **length-propagating dispatcher**: e.g. hfi1
`subn_get_opa_sma(u8 *data, ..., u32 max_len)` is a switch that passes
`max_len` to `__subn_get_opa_*(..., max_len)` -- it is length-AWARE (the bound
travels with the buffer to the callees), but the finder's `hasLengthGuard`
only recognised in-function pskb_may_pull / relational / ceph_decode_need
checks.  Same "incomplete guard recognition" theme as count/index.

`adv_lenprop` (a length-ish parameter len/size/max/count passed as an argument
to a callee) reclassifies ~700 such dispatchers frontier -> locally-aware
(FRONTIER 2940 -> 2242), advisory-only (no drops).

## Remaining: interprocedural skb-pull confirmation

The 2242 frontier is the genuine read-before-length-check set whose length
guard, if any, is NON-LOCAL.  This is settled exactly as count/index's
non-local-validator step, by `caller_precondition.ql`'s skb-pull shape
(interprocedural pskb_may_pull to depth 3): CALLER-GUARDED (pulled in all
callers) vs genuinely UNGUARDED.  Top frontier files are real wire/firmware
parsers (ath fw.c/wmi.c, IB mad.c/cm.c, ntfs3 xattr.c) -- the priority set for
that confirmation pass.

Status: wide triage + advisory refinement done (this commit); the
interprocedural skb-pull confirmation is the next step (mirrors the
count/index validator-confirmation), then recall is already in place.
