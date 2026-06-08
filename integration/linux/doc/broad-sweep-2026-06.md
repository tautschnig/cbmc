# Broad multi-subsystem sweep (task #3)

**Date:** 2026-06-08
**Tree:** linux_6_12 (6.12.87).
**DB:** one combined DB (`/tmp/broad-db`, 170 MiB relations) over
`net/rxrpc net/bridge drivers/net/vxlan net/can net/netfilter net/ceph
net/sched drivers/bluetooth net/x25 net/l2tp` — subsystems we had
**never swept**, chosen to attack the "we-never-pointed-the-sweep-at-X"
problem from the gap analysis.
**Queries:** all six (4 original + the 2 new Gap #1/#2 oracles), now
wired into `scan-subsystem.sh`.

## Purpose

The recent-CVE gap analysis showed several already-fixed 2026 CVEs whose
*shape* our oracles cover but which we missed purely because we hadn't
pointed the sweep at their subsystem.  This sweep confirms the hypothesis
and exercises the two new oracles on fresh territory.

## Surface-gap thesis: CONFIRMED — recent CVEs re-surface once swept

| Candidate | Query | Recent CVE it matches |
|-----------|-------|-----------------------|
| `tcpmss_mt` (net/netfilter/xt_tcpmss.c) | tlv_parse_loop | **CVE-2026-43190** (xt_tcpmss: check remaining length before reading optlen) |
| `br_nd_send` (net/bridge/br_arp_nd_proxy.c) | tlv_parse_loop_helper | **CVE-2026-31682 / -31752** (bridge ND option walk) |
| `pipapo_expand` / `pipapo_step_after_end` (net/netfilter/nft_set_pipapo.c) | tainted_into_fixed_dest | **CVE-2026-43453** area (nft_set_pipapo stack OOB read) |
| `rxkad_verify_response` (net/rxrpc/rxkad.c) | tlv_parse_loop_helper | rxrpc authenticator-parser family (CVE-2026-31637 / -46085) |

All four are in subsystems we had never swept; each matches an
already-fixed recent CVE.  This is **recall validation + a fix for the
surface gap**, not novel discovery (consistent with the
why-no-new-bugs retrospective).

## Other precise-finder (tlv_parse_loop) hits worth triage

`dccp_find_option` (xt_dccp.c), `tcp_find_option` (xt_tcpudp.c),
`nf_ct_sack_adjust` (nf_conntrack_seqadj.c), `synproxy_tstamp_adjust`
(nf_synproxy_core.c), `nft_exthdr_dccp_eval` (nft_exthdr.c),
`fl_dump_key_geneve_opt` (cls_flower.c) — all genuine TCP/DCCP/geneve
option walkers; most carry their own `optlen - i >= …` guards (the
xt_tcpmss loop above shows the idiom), so expect a high guarded-FP rate
under CBMC triage.

## New-oracle behaviour on this set

* **Gap #1 (count/index):** no in-scope hits in these specific
  subsystems (the count/index shape concentrates in
  firmware/driver code — dpaa2, amdkfd, b43, ksmbd — not these net
  parsers).  Validated separately on net/nfc (nci targets/pipe) and the
  synthetic fixture.
* **Gap #2 (skb field before len-check):** no in-scope hits here beyond
  bluetooth (where it found the real CVE-2026-31771); the rxrpc/ceph
  parsers use their own decode helpers rather than raw `skb->data`
  casts.

## Known coverage gaps (honest)

* **rxgk** (`net/rxrpc/rxgk.c`, the 7-CVE CVE-2026-31633…-31696
  authenticator family) is **absent from 6.12** — rxgk landed in 7.x.
  Only `rxkad.c`/`key.c`/`input.c` are present and were swept.  To reach
  the rxgk family, sweep a 7.x tree (linux_mainline / linux_next), where
  the percpu_types.h gate fix (commit 693bf4cb8b) already applies.
* The helper-aware TLV finder is **very noisy** on this set: the
  `net/netfilter/ipset/ip_set_hash_gen.h` template expands to hundreds
  of `hash_*` instantiations.  For broad reporting, prefer the precise
  `tlv_parse_loop` + the two new oracles; reserve the helper finder for
  targeted subsystems.

## Takeaways

1. The surface-gap was real and is now closed for these 10 subsystems:
   sweeping them re-finds tcpmss / br_nd / pipapo / rxkad — recent-CVE
   territory the oracles always covered.
2. Next sweep should target a **7.x tree** to reach rxgk and the other
   constructs absent from 6.12.
3. `scan-subsystem.sh` now runs all six queries; a one-DB-many-targets
   build (as here) is the efficient way to widen coverage — ~40 min for
   10 subsystems vs. per-subsystem rebuilds.
