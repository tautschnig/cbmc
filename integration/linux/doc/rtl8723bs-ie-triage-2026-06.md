# Triage: rtl8723bs IE-walker candidates (#2)

**Date:** 2026-06-08
**Candidates:** the 7 hits from `tlv_parse_loop.ql` on the staging DB —
`rtw_get_wps_ie`, `rtw_get_sec_ie`, `rtw_get_wapi_ie`,
`rtw_restruct_wmm_ie`, `rtw_set_wpa_ie` (rtl8723bs).

## Shape (all share it)

Unbounded 802.11 IE walk, e.g. `rtw_get_sec_ie`:

```c
cnt = _TIMESTAMP_ + _BEACON_ITERVAL_ + _CAPABILITY_;   // = 12
while (cnt < in_len) {
    authmode = in_ie[cnt];
    if (authmode == WLAN_EID_VENDOR_SPECIFIC &&
        !memcmp(&in_ie[cnt+2], wpa_oui, 4)) {           // reads in_ie[cnt+2..+5]
        memcpy(wpa_ie, &in_ie[cnt], in_ie[cnt+1]+2);    // reads in_ie[cnt+1]
    }
    cnt += in_ie[cnt+1] + 2;                            // advance, no fits check
}
```

Bound is only `cnt < in_len`; `in_ie[cnt+1]`, `in_ie[cnt+2..+5]`
(and `in_ie[cnt+6..+9]` in `rtw_get_wapi_ie`) are read with no
`cnt + header <= in_len` check.  CBMC confirms the shape
(`tlv_harness_gen.py` → `harness_buggy` FAILED / `harness_fixed`
SUCCESSFUL).  The IEs come from received management frames, so the
over-read is remotely triggerable (the upstream series calls it
"remote heap info disclosure").

## Classification: KNOWN / in-flight (NOT novel)

Diff vs mainline **7.1-rc7** (`/home/ubuntu/linux_mainline`): the
function bodies are **identical except whitespace** — still unpatched
in the latest tagged tree.  But they are **already known and being
fixed**: a current patch series on linux-staging / stable explicitly
addresses them —

* "fix OOB reads in `rtw_get_sec_ie()`, `rtw_get_wapi_ie()`, and
  `rtw_get_wps_attr()`" (linux-kernel / linux-staging, 2026-05)
* "fix OOB reads and writes in IE/attribute parsing" — same trio
* sibling fixes: `issue_assocreq()`, `join_cmd_hdl()`,
  `OnAssocRsp()`, `update_beacon_info()`, `bwmode_update_check()`
  ("Break if the IE's declared data extends past the buffer end";
  "Fixes: 554c0a3abf21 staging: Add rtl8723bs sdio wifi driver").

So the fixes simply have not landed in the 6.12.87 / 7.1-rc7 snapshots
here; the bugs are real but **in-flight, not previously unknown**.

## Outcome

| candidate | verdict |
|-----------|---------|
| `rtw_get_sec_ie` | real OOB-read shape; **known/in-flight** (named in upstream series) |
| `rtw_get_wapi_ie` | real OOB-read shape; **known/in-flight** (named) |
| `rtw_get_wps_ie` | real OOB-read shape; same family (`rtw_get_wps_attr` named) |
| `rtw_restruct_wmm_ie`, `rtw_set_wpa_ie` | same unbounded-walk class; same driver/series |

No novel finding; **no validation-loop / report warranted** — filing
on an actively-patched staging bug would duplicate in-flight work.

## What this tells us (the honest meta-result)

For the **third** time the pipeline independently rediscovered the
exact functions an active kernel-hardening effort is fixing
(vme_user was ours+fixed; NFC LLCP = CVE-2026-31622; rtl8723bs IE =
this series).  That is strong evidence the finder targets the right
bug class — but it also confirms the earlier expectation-setting:
the picked-over subsystems (and staging, now under a concerted audit)
yield rediscoveries, not 0-days.  Genuine novelty needs (a) the
helper-aware finder (#1, just landed) on parsers syzkaller/the
current audit under-cover, and (b) freshly-added linux-next code (#3).
