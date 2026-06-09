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

## Done (Phase 2 + 3) — interprocedural taint, realised as a confidence tier

`KernelTaintFlow.qll` provides `isRawTaintReachable(Expr)` and the
count/index + TLV-helper oracles now emit a **HIGH / MEDIUM confidence
tier** instead of a binary verdict.

### Why a tier and not a destructive gate
A single `TaintTracking::Global` path-trace is **not usable** here, for
two independent reasons discovered against the real code:

1. **Function-pointer indirection breaks the path.**  The canonical TP —
   `net/can/gw.c cgw_csum_*`, whose array index `crc8->result_idx` is
   bulk-filled from a raw netlink attribute by `nla_memcpy()` (the NLA
   policy only bounds `.len`, not the index fields) — reaches its use
   through `(*mod->csumfunc.crc8)(...)`, an indirect call global dataflow
   cannot follow.  A global gate would *drop* cgw (false negative).
2. **Taint cannot separate the FP from a real TP.**  `dpaa2 num_ifs`
   (CVE-2026-43205) and the mac80211 `params->link_id` FP are
   *structurally identical* — both are a field of a caller-passed struct
   with no modelled wire source.  A hard gate dropping `link_id` would
   also drop the dpaa2 TP (and break the regression fixture).

So `isRawTaintReachable` combines two cheap, indirection-robust signals:
**(a)** intra-procedural taint from a raw source, and **(b)** the value
is a field of a struct bulk-filled from raw bytes anywhere (`nla_memcpy`/
`copy_from_user`/a cast of `skb->data` or `nla_data`).  Signal (b) keeps
cgw despite the function-pointer call.  Raw sources are
`skb->data`/`nla_data`/`nla_memcpy`/`copy_from_user`/`ceph_decode_*`/
unaligned reads **plus byte-buffer-parameter reads** (b43 `desc[1]`);
the typed `nla_get_*` accessors are deliberately NOT raw (policy-bounded)
— the Phase 3 sanitizer call that demotes the `link_id` cluster.

### Results (no recall loss — everything is still reported, just tiered)
* **count/index** on broad-next-db (7.x): 92 hits → **HIGH=4, MEDIUM=88**.
  The 4 HIGH are exactly `cgw_csum_*` `result_idx` (the genuine CAN-gw
  OOB); the 88 MEDIUM are the mac80211 `link_id` FP cluster + dpaa2-style
  recall (taint-inseparable).
* **tlv_parse_loop_helper**: 113 → **HIGH=37, MEDIUM=76**.  HIGH = real RX
  parsers (`crush_decode`, `decode_lockers/watchers`, `ct_sip_get_header`,
  `ieee80211_rx_*`); the TX-option builders (`ieee80211_send_assoc`,
  `ieee80211_build_*`, `rxrpc_send_ACK`) — the TX-writer FP class — are
  MEDIUM.
* Synthetics unchanged: count_index `rx_key_buggy` HIGH / `flood_cfg_buggy`
  MEDIUM (both kept), `bounded_cursor` parse_buggy only, `decoded_len`
  resp_advance/alloc_size/ceph_alloc_size (not plain_len).

### Honest limit
Taint **cannot** distinguish a firmware-filled param-struct count
(dpaa2) from a policy-validated param-struct index (link_id); both sit at
MEDIUM.  Separating them would require modelling per-subsystem validation
(NLA_POLICY ranges, firmware-command response provenance) — out of scope
for a generic AST/local-flow layer.  The HIGH tier is the high-precision
report; MEDIUM preserves recall for triage.

## Files

* `KernelTaint.qll` — sources / cursor shape / sanitizers (Phase 0).
* `KernelTaintFlow.qll` — `isRawTaintReachable` (Phase 2+3).
* `decoded_len_arith_overflow.ql` — Phase 1a.
* `skb_field_before_lencheck.ql` — Phase 1b (imports KernelTaint).
* `tainted_count_into_fixed_array.ql`, `tlv_parse_loop_helper.ql` —
  confidence tier (Phase 2+3).
* `bounded_cursor_test.c`, `decoded_len_test.c`, `count_index_test.c` —
  fixtures.

Watch-out recorded for future query authors: a `*/` sequence inside a
`/** */` QLDoc comment (e.g. `void*/`, `ceph_decode_*/`) silently closes
the comment — reword such doc text.
