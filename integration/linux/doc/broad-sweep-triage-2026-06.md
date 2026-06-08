# Triage of broad-sweep candidates (#2)

**Date:** 2026-06-08
**Inputs:** the in-scope hits from the 6.12 and 7.x broad sweeps that
were surfaced but not adjudicated.
**Question asked:** is any of these a *novel* (not-already-fixed,
not-guarded) bug — or are they all FP / known?

## Verdicts

| Candidate | Oracle | Verdict | Why |
|-----------|--------|---------|-----|
| `can_rcv_filter` (net/can/af_can.c) `can_id`→`rx_sff[2048]` | count/index | **FP** | `can_id &= CAN_SFF_MASK` (0x7FF=2047) before the index — mask-bounded |
| `br_ip4/ip6_multicast_*_report` (net/bridge) `nsrcs`→multiply | decoded-len | **FP/known** | `nsrcs` is `u16` (`ntohs`) so `nsrcs*4` can't overflow 32-bit; `ip_mc_may_pull(skb, len)` bounds it. Well-hardened IGMPv3/MLDv2 parser |
| `nft_parse_register` (net/netfilter) `reg`→multiply | decoded-len | **FP** | `reg * NFT_REG_SIZE` is inside `switch(reg) case NFT_REG_VERDICT..NFT_REG_4` (`default → -ERANGE`); `reg` is case-bounded |
| `__ieee80211_beacon_get` &c (net/mac80211) `link_id`→`link[15]` | count/index | **FP (recall)** | `link_id` is a caller-validated parameter; no in-function guard. Function-granularity recall pattern; caller bounds it against `IEEE80211_MLD_MAX_NUM_LINKS` |
| rxgk family (`rxgk_verify_response`/`_authenticator`, `rxrpc_preparse_xdr_yfs_rxgk`) | decoded-len | **Known CVE** | CVE-2026-31633/-31636/-31641, already fixed in -next; recall validation |

## Bottom line

**No novel bug.**  Every adjudicated candidate is either a false
positive (mask-bound, width-bound, switch-case-bound, or
caller-validated) or an already-fixed known CVE.  This is the expected
result for the swept mainline/LTS code the retrospective described —
and is the strongest argument for pivoting to differential PR-scanning
(#1), where unswept fresh code lives.

## Oracle refinements applied (the triage's concrete payoff)

The FPs were not noise to discard — each named a sound precision
improvement:

1. **Gap #1 (count/index): mask-guard exclusion.**  A masked index
   (`idx &= CONST` / `idx & CONST`) is bounded as soundly as a
   relational guard but uses bitwise-and, so the relational-guard
   predicate missed it.  Added `maskGuarded`.  `can_rcv_filter`
   FP eliminated.

2. **Gap #3 (decoded-len): multiply operand-width filter.**  A 16-/8-bit
   decoded value (e.g. `ntohs → u16`) times a small constant cannot
   overflow the 32-bit arithmetic it promotes to.  `feedsMultiply` now
   requires the decoded value be ≥32-bit.  `br_*_multicast nsrcs` FP
   eliminated; the rxgk round-up family and the u32 synthetic retained.

## Known residual FP class (documented, not fixable cheaply)

The **count/index direct-index** pattern on a *caller-passed* index
(mac80211 `link_id`, ~112 hits) is inherently noisy at function
granularity: the bound lives in the caller, not the function.
Eliminating it soundly needs interprocedural reasoning; eliminating it
heuristically (drop all `arr[param]`) would gut recall.  Left as-is and
flagged here — these should be deprioritised in triage unless the
index is *decoded in the same function*.
