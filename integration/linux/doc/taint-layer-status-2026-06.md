# Taint-source layer — implementation status

**Date:** 2026-06-09
**Plan:** `taint-source-layer-plan-2026-06.md`.

## Done

### Phase 0 — `KernelTaint.qll` (the shared library)
A small, AST-level, tiered taint-source library the ABC oracles consume:

* **decode accessors (sources)** — byte-order/unaligned builtins (to
  which `ntohl`/`ntohs` lower), plus subsystem accessors `ceph_decode_*`
  and `nla_get_*`;
* **`SkbDataAccess`** — a read of `skb->data`;
* **`isBoundedBufferParam`** — the `(void *p, void *end)` / `(buf, len)`
  cursor-parser shape (a void/byte pointer parameter paired with a
  `len`/`size` integral or `end`/`limit` pointer companion);
* **sanitizers (`hasLengthGuard`)** — `pskb_may_pull`,
  `skb_header_pointer`, `nla_validate`/`nla_parse`, the ceph
  `ceph_decode_need` macro (detected via its inner `ceph_has_room()`
  call), and relational `len`/`size`/`end` guards.

Deliberately AST-level (not a full DataFlow config) so it is cheap and
incremental; interprocedural taint can be layered on top (Phase 2+).

### Phase 1a — decode accessors in `decoded_len_arith_overflow`
Added `ceph_decode_*`/`nla_get_*` to the decode-source set.  Newly flags
genuine ceph osdmap/crush decoders that size an allocation by a
wire-decoded count (`crush_decode`, `decode_MOSDOpReply`,
`__decode_pg_temp/upmap_items`, `decode_array_32_alloc` — 6 hits); rxgk
family (19) and synthetic retained.

### Phase 1b — bounded-cursor `skb_field_before_lencheck`
Generalised the oracle from `skb->data`-only to ALSO flag a
bounded-cursor parser (a `(buf,len)`/`(p,end)` function decoding a field
from the cursor with no length guard) — the ceph CVE-2026-43406/43407 /
rxrpc / XDR shape.  On the post-fix 7.x tree the `ceph_decode_need`-
guarded parsers are correctly NOT flagged (via the `ceph_has_room`
sanitizer): bounded-cursor hits dropped 59 → 18, the 18 being
genuinely-unguarded candidates incl. **`rxkad_decrypt_ticket`**
(CVE-2026-31637 area), `decode_lockers`, `try_rfc959` — which the
skb-only oracle missed entirely.

**Validation (Phase 4, partial):** no regression — decoded_len rxgk
(19) + ceph osdmap (6); bounded-cursor `rxkad_decrypt_ticket` (2);
count/index `cgw_csum` (4) retained.  Synthetics: `bounded_cursor_test.c`
(parse_buggy flagged, parse_fixed/not_a_parser excluded),
`decoded_len_test.c` (+ ceph_alloc_size case).

## Scoped next (Phase 2 + 3, intertwined — interprocedural taint)

The remaining precision win — gating `tainted_count_into_fixed_array` so
the `link_id = params->beacon.link_id` FP drops while `ntf->n_targets`
is kept — **cannot be done soundly at AST level**, and is harder than
the plan's Phase 2 framing suggested:

* both `params` (nl80211) and `ntf` (NCI) are *parameters filled by a
  caller*, so within-function AST can't distinguish them;
* even **interprocedural** `TaintTracking::Global` would mark *both* as
  tainted, because `params` is itself derived from a netlink message —
  unless `nla_parse`/policy validation is modelled as a **sanitizer**
  (Phase 3).  So Phase 2 and Phase 3 must land together.

This is the heavyweight, compute-intensive part the plan flagged as the
dominant risk (`TaintTracking::Global` is minutes/query; over-tainting
without sanitizers collapses precision).  It is the right next unit of
work, but a distinct one: build a `DataFlow::ConfigSig` over
`KernelTaint` sources with `nla_parse`/`ceph_decode_need` barriers, gate
the count/index and tlv-helper oracles, and re-measure FP on the broad
sweep.  The precise `tlv_parse_loop` is already tight (3 genuine TCP/SACK
option walkers on 7.x) and needs no gating.

## Files

* `KernelTaint.qll` — the library.
* `decoded_len_arith_overflow.ql` — Phase 1a.
* `skb_field_before_lencheck.ql` — Phase 1b (imports KernelTaint).
* `bounded_cursor_test.c`, `decoded_len_test.c` — fixtures.

Watch-out recorded for future query authors: a `*/` sequence inside a
`/** */` QLDoc comment (e.g. `void*/`, `ceph_decode_*/`) silently closes
the comment — reword such doc text.
