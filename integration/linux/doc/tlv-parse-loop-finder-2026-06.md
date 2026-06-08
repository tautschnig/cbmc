# TLV / parse-loop finder + auto-harness (the capability the NFC sweep needed)

**Date:** 2026-06-08
**Files:** `tlv_parse_loop.ql` (stage 1), `tlv_harness_gen.py` (stage-2
harness generator).
**Motivation:** the NFC LLCP OOB (CVE-2026-31622) was found by *manual*
TLV-loop grep, not by the sharpened single-copy queries — which are
structurally blind to parse loops.  This closes that gap.

## What it finds

`tlv_parse_loop.ql` matches the canonical TLV/IE/option walker:

```c
while (cursor < total) {        // bounded only by `total`
    type = buf[cursor];         // header read
    len  = buf[cursor + 1];     // in-band length
    ... buf[cursor + 2 + i] ...
    cursor += 2 + len;          // advance by a buffer-derived length
}
```

The query requires: a loop whose body (a) reads a value out of `buf`
and (b) advances `cursor` (an `AssignAddExpr`) by an expression that
includes that buffer-read length, with the loop bounded by a simple
`cursor < total` relation.  Missing `cursor + header + value <= total`
checks are the bug; CBMC stage-2 decides reachability by unwinding.

## Validation — re-finds the NFC bug automatically

On `/tmp/nfc-db` (no manual hints):

| function | file:line | note |
|----------|-----------|------|
| `nfc_llcp_parse_gb_tlv` | llcp_commands.c:203 | CVE-2026-31622 |
| `nfc_llcp_parse_connection_tlv` | llcp_commands.c:253 | CVE-2026-31622 |
| `nfc_llcp_recv_snl` | llcp_core.c:1300 | the SNL part of the same fix series |
| `nfc_llcp_connect_sn` | llcp_core.c:853 | TLV walk |

The query surfaced the exact CVE functions **plus** the SNL walker —
more than the manual analysis found.

## Push-button stage 2

`tlv_harness_gen.py` turns a candidate line into a goto-cc/CBMC
harness (canonical buggy + fixed walk).  End-to-end on parse_gb_tlv:

```
tlv_harness_gen.py -c "nfc_llcp_parse_gb_tlv|...|203|cursor=offset|buf=tlv" > h.c
goto-cc -o h.gb h.c
cbmc h.gb --function harness_buggy --bounds-check --pointer-check --unwind 10
  -> VERIFICATION FAILED   (array/pointer OOB, with witness)
cbmc h.gb --function harness_fixed ... -> VERIFICATION SUCCESSFUL
```

## Cross-subsystem results

| DB | TLV-loop hits | notes |
|----|---------------|-------|
| net/nfc | 4 (5 rows) | the CVE walkers (above) |
| net/bluetooth | 0 | **recall gap**: L2CAP option parsing advances via a helper (`l2cap_get_conf_opt`), not a direct in-loop `cursor += buf[i]` |
| drivers/staging | 7 | all rtl8723bs 802.11 IE walkers: `rtw_get_wps_ie`, `rtw_get_sec_ie`, `rtw_get_wapi_ie`, `rtw_restruct_wmm_ie`, `rtw_set_wpa_ie` |

Precision is high — every hit is a genuine TLV/IE walker.  The
rtl8723bs IE walkers are fresh candidates (triaged in #3).

## Known limitation (honest)

The query matches loops whose advance is a **direct** `AssignAddExpr`
in the loop body with an in-line buffer read.  It MISSES walkers that
advance via a **helper function** (Bluetooth L2CAP `l2cap_get_conf_opt`
returns the next option and bumps a pointer) or via pointer
post-increment inside a helper.  Extending to interprocedural advance
(or matching `ptr += helper_return`) is the next refinement; until
then, helper-based parsers need the manual pass.
