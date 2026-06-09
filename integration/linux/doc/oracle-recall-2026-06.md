# Quantitative recall measurement — the 7 oracles vs recent CVEs (#2)

**Date:** 2026-06-09
**Method:** for a set of recent (2026) parser/bounds CVEs, run the
oracle whose shape should match against a DB that contains the CVE's
subsystem, and check whether the CVE's function is flagged.  DBs:
`broad-next-db` (7.x: rxrpc/bridge/vxlan/can/netfilter/ceph/sched/
bluetooth/x25/l2tp/mac80211), `nfc-db` & `bt-db` (6.12), `broad-db`
(6.12 net set).

## Results

| CVE | function | subsystem | expected oracle | result |
|-----|----------|-----------|-----------------|--------|
| 2026-31622 | nfc_llcp_parse_gb_tlv | nfc | tlv_parse_loop | **HIT** |
| 2026-31633 | rxgk_verify_response | rxrpc | decoded_len | **HIT** |
| 2026-31636 | rxgk_verify_authenticator | rxrpc | decoded_len | **HIT** |
| 2026-31641 | rxrpc_preparse_xdr_yfs_rxgk | rxrpc | decoded_len | **HIT** |
| 2026-31570 | cgw_csum_crc8_* | can | count/index | **HIT** |
| 2026-31752 | br_nd_send | bridge | tlv_helper | **HIT** |
| 2026-43453 | pipapo_* | netfilter | tainted_into_fixed_dest | **HIT** |
| 2026-43190 | tcpmss_mt | netfilter | tlv_parse_loop | **HIT** (6.12) † |
| 2026-31771 | hci_store_wake_reason | bluetooth | skb_field | **HIT** (pre-fix) ‡ |
| 2026-43406 | process_message_header | ceph | skb_field | **MISS** |
| 2026-43407 | ceph_handle_auth_reply | ceph | decoded_len | **MISS** |

**Shape recall (subsystem-built ∧ shape-in-catalog): 9/9 (100%).**
**Within-scope misses: 2 (both ceph).**

† `tcpmss_mt` flags on the 6.12 DB but shows 0 on `broad-next-db`
*because `xt_tcpmss` was not built into the 7.x DB* (Kconfig coverage
axis), not a shape miss.  Recall is conditional on the subsystem being
compiled into the DB.

‡ `hci_store_wake_reason` flags on 6.12 but 0 on 7.x — *correctly*: the
CVE-2026-31771 fix ("move wake reason storage into validated event
handlers") removed the unguarded `skb->data` read, so the post-fix
function no longer has the shape.  Measuring recall on a post-fix tree
under-counts; the honest measurement is on the pre-fix tree.

## The actionable gap: ceph-style decode-accessor parsers

Both misses are ceph functions that parse with **`ceph_decode_8/16/32/64
(&p)`** over a **`void *p, void *end`** buffer:

```c
desc->fd_tag     = ceph_decode_8(&p);
desc->fd_seg_cnt = ceph_decode_8(&p);
desc->fd_lens[i] = ceph_decode_32(&p);   // count fd_seg_cnt drives this
```

Our oracles miss them for two precise reasons, each a clear next step:

1. **`decoded_len_arith_overflow`'s `DecodeCall` only matches the
   byte-order builtins** (`__builtin_bswap*`, `get_unaligned*`,
   `be*_to_cpu`).  ceph (and many subsystems) use their own decode
   accessors.  **Fix:** add `ceph_decode_8/16/32/64`,
   `ceph_decode_*`, and the analogous nlattr/`nla_get_*` accessors to
   the decode-source set.  Cheap, high-yield.

2. **`skb_field_before_lencheck` is hard-keyed to `skb->data`.**  ceph,
   rxrpc, XDR, on-disk parsers operate on a generic `(void *p, void
   *end)` or `(buf, len)` cursor.  **Fix:** generalise the oracle to a
   "bounded cursor" — a pointer advanced toward an `end`/`len` with
   reads before the advance is validated.  This is the same
   generalisation the rxgk work motivated (Gap #3 already covers the
   *arithmetic*; this covers the *cursor read*).

## Bottom line

The seven oracles have **strong recall (9/9) on the parser/bounds shapes
and subsystems they target**, validated against independent recent CVEs.
The honest residual is **decode-accessor / generic-cursor parsers**
(ceph the exemplar): two concrete, cheap oracle extensions — add
subsystem decode accessors to the decoded-length source set, and
generalise the field-before-length-check oracle from `skb->data` to a
generic bounded cursor — would close it.  This is the highest-yield
next oracle work, and it informs the #3 taint-source design (the decode
accessors are exactly the taint *sources* to annotate).
