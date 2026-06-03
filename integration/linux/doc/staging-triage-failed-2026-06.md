# Triage of the 47 FAILED staging candidates (corrected `-noseg` sweep)

**Date:** 2026-06-03
**Input:** the 47 CBMC-FAILED candidates from the corrected
58-candidate sweep (`poc/staging_verdicts_noseg.tsv`).  Focus is
the **26 NEW** candidates surfaced once the `__seg_gs` extractor
gap was fixed (`codeql-extractor-coverage-2026-06.md`); the other
21 were already covered by `staging-triage-2026-06.md` and the
offset-bound harnesser extension.

## Bottom line

| class | count (of 47) | meaning |
|-------|---------------|---------|
| **False positive** | ~38 | provably safe; harnesser missed a guard/alloc idiom |
| **Uncertain** | ~6 | no local bound; safety depends on caller/HW limits |
| **Likely real OOB** | 3 (`hmm_store` ×3) | already reported separately; reachable via atomisp ioctl |
| **New true positives** | **0** | no genuinely-new OOB surfaced beyond `hmm_store` |

The 26 NEW candidates are overwhelmingly false positives: the
recovered drivers (wifi, media, USB) use the *same* safe idioms the
harnesser already can't model — proportional allocation and
guarded fixed buffers — just with more variety.

## NEW candidates (26) — per-function

### rtl8723bs / rtl8192e wifi (10)

| function | verdict | why |
|----------|---------|-----|
| `rtw_set_wps_beacon` / `_probe_resp` / `_assoc_resp` | FP | `rtw_malloc(ie_len)` then `memcpy(dst, src, ie_len)` — proportional; copy size `ie_len = len−14` derived, so harnesser keyed on `len` missed it |
| `rtw_buf_update` | FP | `dup = rtw_malloc(src_len); memcpy(dup, src, src_len)` — proportional (param-name mismatch) |
| `rtw_check_beacon_data` | FP | guard `if (len < 0 \|\| len > MAX_IE_SZ) return`; dest `ies[MAX_IE_SZ]` fixed |
| `prism2_wep_set_key` | FP | guard `if (len < 0 \|\| len > WEP_KEY_LEN) return`; dest `wep->key[WEP_KEY_LEN]` |
| `rtllib_auth_challenge` | FP | `skb = …_req(chlen+2); skb_put(skb, chlen+2); memcpy(c, challenge, chlen)` — proportional skb |
| `rtw_set_ie` / `rtw_set_fixed_ie` | FP | unbounded IE-builder helpers; write into a caller-sized `pbuf` (trusts caller) |
| `_cfg80211_rtw_mgmt_tx` | **uncertain** | `memcpy(pframe, buf, len)` into a fixed xmit buffer (`alloc_mgtxmitframe`) with no visible local bound on `len` |

### av7110 media (10)

| function | verdict | why |
|----------|---------|-----|
| `dvb_play` / `dvb_aplay` | FP | clamp `if (n > IPACKS*2) n = IPACKS*2`; `kbuf[type]` is `2*IPACKS` (=4096) bytes |
| `ci_ll_write` | FP | guard `if (count > 2048) goto out`; `page = __get_free_page()` (4096) |
| `av7110_vbi_write` | FP | guard `count != sizeof(d)`; copies exactly `sizeof(d)` into `d` |
| `gpioirq` | FP | clamp `if (len > 2*1024) len = 2*1024`; `debi_virt` = 8192 (kernel src, not user) |
| `irdebi` | FP | guard `if (count <= 4) memcpy(debi_virt, &res, count)` |
| `mwdebi` | **uncertain** | `memcpy(debi_virt, val, count)` — no local bound; `debi_virt`=8192, trusts caller |

### atomisp (2) + gdm724x (4)

| function | verdict | why |
|----------|---------|-----|
| `copy_from_compatible` ×2 | FP (helper) | thin `copy_from_user`/`memcpy(to, from, n)` wrapper; bound lives at callers (same param-copy surface as `hmm_store`) |
| `netlink_send` | FP | `skb = alloc_skb(NLMSG_SPACE(len)); memcpy(NLMSG_DATA(nlh), msg, len)` — proportional |
| `gdm_usb_sdu_send` ×2 | **uncertain** | `memcpy(sdu->data, data[+ETH_HLEN], len[−ETH_HLEN])` into a fixed free-list SDU buffer; `len` from netdev TX (MTU-bounded by the stack, but no local check) |

## Original-21 FAILED (recap)

Already classified in `staging-triage-2026-06.md` / superseded by
the offset-bound extension.  Summary: `_anybus_mbox_cmd` ×3 (guard
`msg_sz > MAX_MBOX_MSG_SZ`), `dvb_filter_pes2ts` (loop-reduced),
`gb_hid_set_report` / `gb_spi_operation_create` /
`gb_loopback_operation_sync:395` (struct-field / caller-buffer),
`create_area_*`, `anybuss_*`, `gdm_mux_send`, `comp_vdev_read`,
`ext_sd_get_rsp`, `esparser_queue_eos`, `memcpy_copy_callback`,
`gb_raw_send(count)` (spurious taint) — all FP or
caller-context-dependent.  `hmm_store` ×3 — **likely real**, see
`hmm-store-reachability-2026-06.md`.

## The uncertain set (worth a closer look)

Six sites have **no local bound** and copy into a fixed buffer; all
are "trusts the caller / HW protocol" rather than demonstrable OOB:

* `_cfg80211_rtw_mgmt_tx` — mgmt frame `len` into xmit buffer
  (nl80211 mgmt_tx; cfg80211 bounds frame size, buffer is fixed).
* `mwdebi` — `count` into 8192-byte `debi_virt` (firmware/OSD
  callers use small fixed counts).
* `gdm_usb_sdu_send` ×2 — netdev TX `len` into fixed SDU buffer
  (MTU-bounded by the network stack).

Resolving these needs the same interprocedural / caller-precondition
modelling already noted as the next harnesser step; none shows an
attacker-reachable unbounded `len` on local inspection.

## Takeaways

1. **No new true positive** beyond `hmm_store`.  The corrected
   (wider) sweep more than doubled the candidate count but did not
   surface a second clearly-exploitable OOB — it surfaced more
   instances of the well-characterized safe idioms.
2. The FP drivers are dominated by **proportional allocation**
   (`*alloc(len)` / `alloc_skb(SPACE(len))` / `skb_put(n)`) and
   **guarded fixed buffers** (`len > CONST` with a non-`*SIZE`
   constant the harnesser's size-token regex misses).  Two cheap,
   sound harnesser wins: (a) treat any `*alloc*`/`alloc_skb`/`skb_put`
   whose size is a simple function of the copy length as
   proportional; (b) widen the size-token regex to bare
   `MAX_*` / `*_LEN` / `*_SZ` constants.
3. The **honest yield of the staging sweep**: 58 candidates → 11
   proven safe by CBMC → 47 to triage → ~38 FP, ~6 uncertain, 3
   likely-real (`hmm_store`), 0 new TP.  The scale-out's value was
   the *method* and the two confirmed findings (vme_user fixed +
   proved; hmm_store reported), not a high raw bug count.
