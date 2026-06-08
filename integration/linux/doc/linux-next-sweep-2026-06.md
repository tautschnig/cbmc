# linux-next sweep (#3)

**Date:** 2026-06-08
**Tree:** `linux-next` next-20260605 (7.1.0-rc6 + next), shallow-cloned
to `/home/ubuntu/linux_next`.
**DB:** net/nfc + net/bluetooth (16 + 21 function-bodies after the
extractor fix below).
**Queries:** all four — `tlv_parse_loop`, `tlv_parse_loop_helper`,
`tainted_into_fixed_dest`, `tainted_alloc_overflow`.

## Extractor fix: the __seg_gs gate MOVED in 7.1

First build extracted almost nothing (6.5 MiB relations, 4+1 bodies):
the same `__seg_gs` `current.h` parse failure, because in 7.1 the
named-address-space gate **moved** from `percpu.h`
(`#ifdef CONFIG_CC_HAS_NAMED_AS`) to **`percpu_types.h`**
(`#if defined(CONFIG_SMP) && defined(CONFIG_CC_HAS_NAMED_AS)`).  The
recipe's sed didn't match the new form.

Patched `percpu_types.h:5` → relations jumped to 28.5 MiB, zero
`current.h` errors, coverage restored (16 nfc + 21 bt bodies).
`build-codeql-db.sh` now patches **both** gate locations and reverts
both — version-robust across 6.x and 7.x.

## Results (after the fix)

| query | hits (nfc/bt) |
|-------|---------------|
| tlv_parse_loop | NFC LLCP walkers: `parse_gb_tlv`, `parse_connection_tlv`, `recv_snl`, `connect_sn` |
| tlv_parse_loop_helper | L2CAP: `l2cap_parse_conf_req/_rsp`, `l2cap_conf_rfc_get`; + `nci_hci_send_data`, `hci_*_evt`, `sco_sock_getsockopt` |
| tainted_into_fixed_dest | `process_adv_report`, `store_pending_adv_report` (max_adv_len-guarded), `mgmt_mesh_add`, `nci_set_config_req` (BUG_ON-guarded) |
| tainted_alloc_overflow | none |

## Triage — no novel finding in linux-next

* **NFC LLCP** walkers: identical unpatched code as 6.12/7.1-rc7 →
  same known/in-flight CVE-2026-31622 series (see
  `nfc-llcp-tlv-oob-2026-06.md`).  Not novel.
* **L2CAP** `l2cap_parse_conf_req`: now visible (helper-aware finder
  win), but bounded at the loop level —
  `len -= l2cap_get_conf_opt(&req,..); if (len < 0) break;`.  A
  possible ≤4-byte in-helper read of `opt->val` before the check, but
  this is heavily-audited L2CAP code with known historical
  CVEs/hardening; not a clear novel bug.
* **`nci_hci_send_data`**: a fragmentation/**sender**, not a parser;
  `data_len` is the caller's own buffer length → FP of the
  helper-aware finder (which is recall-tuned/looser).
* **fixed-dest** hits: all guarded (max_adv_len == destArr size;
  `BUG_ON(len > NCI_MAX_PARAM_LEN)`), as on the other trees.

## Conclusion

The pipeline now runs end-to-end on the freshest tree (linux-next),
once the 7.1 gate-location fix is applied.  No novel OOB surfaced in
the swept net subsystems — the hits are the known/in-flight NFC
series, guarded fixed-dest copies, well-audited L2CAP, and one
sender-FP.  This is the expected outcome for net/nfc + net/bluetooth
(heavily fuzzed + under active audit).  The remaining novelty levers
are: broaden the *oracle* (#4 — write-OOB / underflow finds different
bugs in the loops we already see) and widen the *surface* (sweep
newly-added -next drivers beyond the classic parsers).
