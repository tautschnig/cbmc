# TLV-finder sweep across remote-input parsers (#3)

**Date:** 2026-06-08
**Tool:** `tlv_parse_loop.ql` + `tlv_harness_gen.py` (from #1), via the
`build-codeql-db.sh` recipe.

## Subsystems swept

| subsystem | DB | TLV-loop hits | fixed-dest | alloc-ovf |
|-----------|----|---------------|-----------|-----------|
| net/nfc (LLCP) | nfc-db | 4 (CVE-2026-31622 + SNL) | 1 (FP) | 0 |
| net/mctp + net/can + net/ieee802154 + net/6lowpan | netparsers-db | **0** | 0 | 0 |
| drivers/usb/core | usbcore-db | **0** | — | — |
| net/bluetooth | bt-db | 0 | 3 (FP) | 1 (FP) |
| drivers/staging (rtl8723bs) | staging-db-noseg | **7** | 6 (FP) | 14 (FP) |

## Genuine candidates: rtl8723bs 802.11 IE walkers (7)

`rtw_get_wps_ie`, `rtw_get_sec_ie`, `rtw_get_wapi_ie`,
`rtw_restruct_wmm_ie`, `rtw_set_wpa_ie` — all the classic unbounded
IE walk.  e.g. `rtw_get_wps_ie` (rtw_ieee80211.c:668):

```c
while (cnt < in_len) {
    eid = in_ie[cnt];
    if (eid == WLAN_EID_VENDOR_SPECIFIC &&
        !memcmp(&in_ie[cnt+2], wps_oui, 4)) {   // reads in_ie[cnt+2..+5]
        memcpy(wps_ie, &in_ie[cnt], in_ie[cnt+1]+2);
    }
    cnt += in_ie[cnt+1]+2;                       // in_ie[cnt+1] read, advance
}
```

Bound is only `cnt < in_len`; `in_ie[cnt+1]` and `in_ie[cnt+2..+5]`
are read with no `cnt + header <= in_len` check → OOB read when an
element sits within a few bytes of the end.  CBMC confirms the shape
(generated harness): `harness_buggy` → **FAILED** (array/pointer
OOB), `harness_fixed` → **SUCCESSFUL**.

### Honest caveats
* **Staging / lower impact.** rtl8723bs is staging (slated for
  eventual removal); these `rtw_get_*_ie` helpers are widely copied
  across rtl drivers and have seen prior hardening — so some/all may
  already be known or fixed in mainline.  Reachability with a buffer
  sized exactly `in_len` (vs a padded frame buffer) needs the caller
  trace; the in_ie usually points into a received management frame,
  which makes the over-read remotely triggerable in principle.
* This is the *same class* as the NFC CVE, found by the same query —
  good evidence the finder generalizes.

## Why the net parsers came back empty (honest)

mctp/can/802154/6lowpan/usb-core returned **0** TLV-loop hits.  Two
reasons, both the documented recall gap, not necessarily "clean":
* **Helper-based advance.** USB descriptor parsing
  (`usb_parse_configuration`) and Bluetooth L2CAP
  (`l2cap_get_conf_opt`) advance the cursor via a helper return, not
  an in-loop `cursor += buf[i]`; the query only matches the direct
  `AssignAddExpr` form.
* CAN isotp/j1939 reassembly is offset/state-machine driven across
  frames rather than a single in-buffer TLV walk.

Closing this needs the interprocedural-advance extension (match
`cursor += helper(...)` / pointer post-increment in a callee).  Until
then these subsystems need a manual pass or a helper-aware query.

## Takeaways

* The finder has **high precision** (every hit is a real TLV/IE
  walker) and **moderate recall** (direct-advance loops only).
* Fresh candidates this round: the 7 rtl8723bs IE walkers (staging,
  same class as the confirmed NFC CVE).
* The biggest recall win available is the **helper-aware extension**,
  which would unlock L2CAP and USB descriptor parsing — the two
  highest-value remote/removable surfaces that came back empty here.
