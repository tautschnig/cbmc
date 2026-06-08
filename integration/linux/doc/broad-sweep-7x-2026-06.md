# 7.x (linux-next) broad sweep

**Date:** 2026-06-08
**Tree:** linux-next next-20260605 (7.1.0-rc6+next).
**DB:** combined over `net/rxrpc net/bridge drivers/net/vxlan net/can
net/netfilter net/ceph net/sched drivers/bluetooth net/x25 net/l2tp
net/mac80211` (rxrpc built with `CONFIG_AF_RXRPC=m`, `RXKAD=y`,
`RXGK=y` so the rxgk family compiles).
**Queries:** all six (4 original + Gap #1/#2).

## Headline: the new oracles fire productively on 7.x

| Candidate | Query | Note / CVE |
|-----------|-------|------------|
| `cgw_csum_crc8_pos/neg`, `cgw_csum_xor_*` (net/can/gw.c) → `data[64]` | tainted_count_into_fixed_array | **CVE-2026-31570** area (can gw OOB via result_idx/from_idx) |
| `can_rcv_filter` (net/can/af_can.c) `can_id` → `rx_sff[2048]` | tainted_count_into_fixed_array | CAN RX filter index |
| `__sta_info_alloc` `n_bitrates` → `supp_rates[6]` | tainted_count_into_fixed_array | mac80211 supported-rates count |
| mac80211 `link_id` → `link[15]` / `rates_idx` → `rates[4]` (tx/scan/chan/vht/mlme) | tainted_count_into_fixed_array | MLO link-id index family |
| mac80211 rx/wpa, can/bcm `skb->data` reads | skb_field_before_lencheck | 802.11 frame + CAN BCM parsers |
| `pipapo_expand` / `pipapo_step_after_end` (nft_set_pipapo.c) | tainted_into_fixed_dest | CVE-2026-43453 area (7.x) |
| `tcp_find_option`, `nf_ct_sack_adjust` | tlv_parse_loop | TCP/SACK option walkers (7.x) |
| `rxkad_verify_response` | tlv_parse_loop_helper | rxrpc rxkad authenticator |

The two new oracles (Gap #1 count/index, Gap #2 skb-field) clearly
earn their place: they light up mac80211 and CAN — subsystems where
the original four found little — and re-surface the can-gw csum family
(CVE-2026-31570 territory).

## rxgk IS reachable on 7.x — but our oracles don't flag it (new gap)

Correcting the earlier session note: with `RXGK=y`, **rxgk.c extracts
fine** (`rxgk_verify_response`, `rxgk_verify_authenticator`,
`rxrpc_preparse_xdr_yfs_rxgk` all present — 495 net/rxrpc bodies,
46 rxgk rows).  The 7-CVE rxgk family is *reachable*.

But **none of the six oracles flag rxgk_verify_response** (the
CVE-2026-31633 integer-overflow / -31635 oversized-auth-length
function).  Reading the code shows why: rxgk parses a generic
`(void *buffer, unsigned int len)` argument (not `skb->data`), and its
length math uses `xdr_round_up(resp_token_len)` — a `(n+3) & ~3`
round-up that can integer-overflow — feeding `> len` bounds checks.
That shape is:

* not `skb->data` → **Gap #2 misses** (keys on `skb->data`);
* not a TLV cursor loop → tlv_parse_loop misses;
* not a fixed-array copy/index → tainted_into_fixed_dest / Gap #1 miss;
* not a `kmalloc(a*b)` → tainted_alloc_overflow misses.

**This is a precise new gap (Gap #3 candidate):** a *generic
buffer+len parser* oracle — a length field read from a `(void *buf,
unsigned int len)` buffer that feeds round-up/multiply arithmetic
(`xdr_round_up`, `roundup`, `* n`) used as a bound or copy size.
This generalises skb_field_before_lencheck beyond `skb->data` and
overlaps the integer-overflow-in-length class.  It is the right oracle
to catch the rxgk / XDR / ASN.1 / on-disk-structure parser family that
CBMC's arithmetic reasoning is uniquely suited to.

## Two process corrections (honest record)

1. **Diagnostic queries must live in the qlpack dir.**  A chunk of this
   session was wasted chasing a phantom "0 bodies / extraction failed"
   result.  Root cause: ad-hoc probe queries written to `/tmp/` fail to
   resolve `import cpp` (no qlpack context) and silently return empty.
   The *real* oracle queries (run from `abc-refinement/`) worked the
   whole time.  Lesson: put probes in `abc-refinement/`, and trust a
   real-query result over a bare-`import cpp` probe.

2. **Recipe clean-step bug (fixed).**  `build-codeql-db.sh` cleaned
   objects with `find "$TREE/$TARGET"`, which treats a multi-target
   string (`"net/a/ net/b/"`) as one nonexistent path and cleans
   nothing — leaving stale `.o` that make would skip, yielding empty
   extraction.  Now loops per target (and the coverage self-check
   likewise).  This did NOT cause the phantom above, but it is a real
   latent bug for multi-target builds and is now fixed.

## Conclusion

* 7.x extraction works end-to-end, rxgk included.
* The Gap #1/#2 oracles add real value on 7.x (mac80211, CAN), incl.
  the can-gw csum CVE-2026-31570 area.
* The rxgk family motivates **Gap #3**: a generic buffer+len /
  length-arithmetic-overflow parser oracle.  That is the highest-value
  next oracle and squarely in CBMC's wheelhouse.
