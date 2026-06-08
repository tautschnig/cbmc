# NFC LLCP TLV parser OOB read — rediscovered CVE-2026-31622

**Date:** 2026-06-08
**Target:** `net/nfc/llcp_commands.c:192` (`nfc_llcp_parse_gb_tlv`)
and `:242` (`nfc_llcp_parse_connection_tlv`)
**DB:** `/tmp/nfc-db` (24/24 coverage, built via `build-codeql-db.sh`)

## Discovery path

1. `tainted_into_fixed_dest.ql` flagged `nci_set_config_req:232`
   (the only NFC hit from that query; triaged as FP — guarded by
   `BUG_ON(param->len > NCI_MAX_PARAM_LEN)` matching the dest
   array size).
2. Manual inspection of the NFC LLCP TLV walkers (identified by
   grep for the `while (offset < tlv_array_len)` pattern) found
   the actual bug — `nfc_llcp_parse_gb_tlv` and
   `nfc_llcp_parse_connection_tlv`:
   - `offset` is `u8` (wraps at 255), `tlv_array_len` is `u16`
     (up to 65535) — **u8 overflow causes infinite loop / OOB**
   - `tlv[0]`/`tlv[1]` read without checking `offset + 2 <=
     tlv_array_len` — **OOB read when < 2 bytes remain**

## CBMC confirmation

`nfc_llcp_tlv_harness.c` (scaled model, BUFMAX=8):

| harness | verdict | property |
|---------|---------|----------|
| `harness_buggy` | **FAILED** | `pointer outside object bounds in tlv[0]` + `tlv[1]` |
| `harness_fixed` (u16 offset + header/value bounds) | **SUCCESSFUL** | — |

Demonstrates the goto-cc / loop-unwinding approach finding parse-loop
OOBs that the single-copy scaled model cannot express.

## Upstream status

This is **CVE-2026-31622**, independently discovered and fixed in
mainline (June 2026 stable backport). The fix is exactly what our
harness models:
- Widen `u8 offset` to `u16`
- Add `offset + 2 <= tlv_array_len` (header fits)
- Add `offset + 2 + length <= tlv_array_len` (value fits)

Our tree (6.12.87, May 2026) predates the fix, so the bug is
present and confirmed by CBMC.

## Significance for the pipeline

This validates the full methodology for a **new, real, security-
relevant** finding class (remote OOB read via NFC RF):
1. DB built with the recipe (`build-codeql-db.sh`, percpu.h fix,
   coverage check: 24/24)
2. Sharpened query identified the subsystem surface; manual TLV-
   pattern grep found the parse loops the query doesn't yet cover
3. `goto-cc` + CBMC `--unwind` confirmed the OOB with a precise
   failing property (`pointer outside object bounds`)
4. The fix harness proves safety

The TLV-loop-finder CodeQL query (proposed #4 in the next-steps)
would have surfaced this automatically — that's the natural
extension.
