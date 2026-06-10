# Caller-precondition automation

**Date:** 2026-06-09. Automates the dominant residual false-positive class
identified by the end-to-end evaluation: function-granularity oracle hits
whose bound is validated not in the flagged function but in its **callers**.

## The query — `caller_precondition.ql`

For a function with a **bound** -- either a `len`/`size`/`count`-named
integral parameter, or a `(p, end)` / `(p, limit)` pointer cursor (ceph /
XDR / rxrpc style) -- it inspects every call site and asks whether the
bound is established by the caller before the call.

For the **scalar** shape that means a *validate-then-reject* guard in the
caller:

```c
if (V <relop> CONST) { return ...; }   // or goto / break
... callee(..., V, ...);               // guard precedes the call
```

Aggregated per function: `callers`, `guarded` (call sites where the bound
arg is so guarded), and a `verdict`:

* **CALLER-GUARDED** — every caller validates the bound → the
  function-granularity hit is an FP the callers resolve.
* **PARTIAL** — some callers validate, some don't.
* **UNGUARDED** — no caller validates → genuine concern (or the bound is
  used safely inside the function for other reasons).

The guard model is AST-level (the kernel's pervasive validate-then-reject
idiom) but the "before the call" test is real **control-flow dominance**
(`strictlyDominates`): the guard's condition must dominate the call, so
every path reaching the call has passed the guard. This is sound for the
dismissal use (a CALLER-GUARDED verdict means the guard provably runs),
not a line-order approximation.

## Validation (broad-next-db, net/)

* **`rxkad_decrypt_ticket` → CALLER-GUARDED** (callers=1, guarded=1) —
  reproduces, automatically, the manual finding that the OOB flags-read is
  closed by `rxkad_verify_response`'s `ticket_len >= 4` check (rxkad.c:1167).
* Distribution over the surface: scalar **24 CALLER-GUARDED / 35 PARTIAL /
  812 UNGUARDED**; cursor **51 CALLER-GUARDED / 12 PARTIAL / 45 UNGUARDED**
  (108 cursor functions the check classifies on top of the scalar case).
  Switching the guard test from line-order to control-flow dominance moved
  6 scalar functions out of CALLER-GUARDED (e.g. `memdup_sockptr_noprof`,
  the mesh `*_size_ok` helpers) -- their textually-preceding guard sat in a
  sibling branch and did not dominate the call, so line-order had
  over-credited them (a potential false dismissal now avoided).
* Cross-referenced against the 12 bounded-cursor oracle hits: rxkad is the
  one CALLER-GUARDED scalar (auto-dismissed FP); `decode_lockers` is now
  classified `cursor`/UNGUARDED (its caller sets `end = p + reply_len` but
  does no `ceph_decode_need`, so it must self-guard); `nft_payload_n2h`
  resolves to `len-param`/UNGUARDED; the remaining genuine concerns
  (`ieee80211_get_ttlm`, `ieee80211_key_alloc`, `nf_nat_ipv4/6_csum_recalc`)
  stay UNGUARDED. Only the non-cursor, non-len functions (`addr_match`, the
  TX builders) remain unmatched — they are not bounded parsers.

## Integration

`pipeline_eval.py` runs the query once, builds a `func → verdict` map, and
adds a **`caller`** column to the distilled-survivor table:

```
function              ... src   shape:b/f   reach:b/f    caller          ground-truth
rxkad_decrypt_ticket  ... REAL  FAIL/SUCC   REACH/BLOCK  CALLER-GUARDED  rxkad ticket parse
ieee80211_get_ttlm    ... REAL  FAIL/SUCC   REACH/BLOCK  UNGUARDED       mac80211 T2L map
try_rfc959            ... REAL  SUCC/SUCC   BLOCK/BLOCK  UNGUARDED       nf_conntrack_ftp
cgw_csum_crc8_pos     ... REAL  FAIL/SUCC   REACH/BLOCK  no-len-param    CVE-2019-3701
```

So a survivor that is `shape FAILED` (bug shape present) but
`CALLER-GUARDED` is flagged as a caller-resolved FP **without needing a
verbatim harness** — and one that is `UNGUARDED` (ttlm) is kept as a
genuine concern.

## Two parameter shapes

### Scalar bound (`len`/`size`/`count`)
The caller validates the bound argument with a `if (V <relop> CONST)
{ return | goto | break }` guard before the call (the rxkad case).

### `(p, end)` / `(p, limit)` cursor
A pointer cursor parameter plus an `end`/`limit` pointer parameter (ceph /
XDR / rxrpc style).  The caller is "guarded" if, before the call, it
establishes a byte-availability bound — a `ceph_decode_need` /
`ceph_has_room` / `pskb_may_pull` / `*_safe` check, or a relational on the
`end`/`limit` cursor.  **Necessary-not-sufficient**: it bounds the first
reads, not an arbitrary multi-field sub-decode, so an UNGUARDED cursor
verdict is the reliable one (the caller does *not* pre-check, so the
function must self-guard) while CALLER-GUARDED is a heuristic prior.

### Struct-field bound (producer-side)
The index is a *struct field* (`arr[s->fld]`, e.g. cgw
`cf->data[crc8->result_idx]`), not a call argument — so the validation
site is the **producer** (where the struct is filled from the wire), not a
caller.  Verdict `PRODUCER-GUARDED` when the field's struct is bulk-filled
from raw netlink/user bytes (`nla_memcpy`/`copy_from_user`) AND the field
is validated by a `chk`/`check`/`validate`/`verify` call or a relational
bound on the same field (linked by field identity).  cgw is the canonical
case: `cgw_parse_attr` calls `cgw_chk_csum_parms(c->from_idx, c->to_idx,
c->result_idx, ...)` -- bounding the indices against the CAN frame -- right
before the `nla_memcpy` that stores the struct, so all four `cgw_csum_*`
consumers are PRODUCER-GUARDED.

### `skb` pull (caller `pskb_may_pull`), interprocedural
The function takes a `struct sk_buff *` and reads `skb->data` with no
in-function length check (the skb oracle's `skb->data` shape).  The
validation is an ancestor pulling the skb: a dominating `pskb_may_pull` /
`skb_may_pull` / `skb_header_pointer` call, or an `skb->len` relational, on
the skb threaded down to the function.  Because `pskb_may_pull` is usually
done **once high in the rx stack**, the check is **interprocedural to depth
3**: CALLER-GUARDED holds only when *no* call path within three frames
reaches the function with the skb unpulled (`hasUnpulledPath` is the
positive witness, negated once).  It is conservative — an skb whose origin
in a frame cannot be traced to a forwarded, pulled parameter counts as
unpulled — so CALLER-GUARDED never wrongly certifies a function.  Depth-3
resolves the classic dispatch pattern (`ieee80211_scan_rx`,
`j1939_xtp_rx_*`, `icmp_manip_pkt`: pulled at the rx entry, then handed to
per-message handlers).  `pipeline_eval` still keeps any residual skb-pull
UNGUARDED in a separate `skb-pull-weak` bucket (depth > 3, or untraceable
origin), not the high-confidence genuine set.

## Honest limits

* **Producer-side validation is field-identity-based, not flow-precise** —
  PRODUCER-GUARDED links a consumer's index field to a validation of the
  *same field* anywhere in the program; it does not prove the validated
  instance is the one consumed. Sound enough to rank, not to prove.
* **Cursor CALLER-GUARDED is a heuristic prior, not a proof** — it shows
  the caller pre-checks the first reads, not that a multi-field sub-decode
  stays in bounds. The UNGUARDED cursor verdict is the dependable one.
* The scalar guard requires its reject branch to exit and its condition to
  strictly dominate the call, and uses **SSA value-flow** so a guard whose
  bound variable is reassigned between guard and call (or cannot be
  SSA-proven stable, e.g. address-taken) is not credited — no stale-guard
  false dismissals. The cursor/skb-pull shapes still match by variable
  identity (their guards are calls / `end` relationships, less prone to the
  scalar-reassign hazard).

## Shape 5 — validate-at-storage-then-parse-later (non-local validator)

**Date:** 2026-06-10. Added after triaging the survey's lone verbatim
REAL+reachable hit, `ieee80211_get_ttlm`, to ground truth: a confirmed
false positive whose length guard sits **two layers up**, at the point the
element is stashed into an array — not in the immediate caller, so the four
local shapes above all report UNGUARDED.

The recognised idiom (pervasive in mac80211/cfg80211 element parsing):

```c
/* storage gate, parse.c */
if (ieee80211_tid_to_link_map_size_ok(data, len) && n < ARRAY_SIZE(e->ttlm))
    e->ttlm[n++] = (void *)data;          // only length-validated elements land here
...
/* later walk */
parse_adv_t2l(.., e->ttlm[i], ..);        // -> pos = ttlm->optional
                                          //    -> get_ttlm(map_size, pos)
```

The query recognises it structurally:

* **validatedStorageField(F)** — a store `s->F = data` (or `s->F[..] = data`)
  gated by an `if` whose condition calls a `*_ok` / `*_size_ok` / `*_check`
  / `*may_pull` validator **applied to the stored value** *and* taking a
  length/size (integral) argument. The integral-arg requirement is what
  keeps `CONFIG_DEBUG_LIST` primitives (`__list_add_valid`, all-pointer
  args) out.
* **validatedParam(f, p)** — every call site of `f` passes a
  validatedStorageField element for `p` (e.g. `parse_adv_t2l(.., e->ttlm[i])`).
* **derivedFromValidatedParam / validatedPointerArg** — a local assigned
  from a validated param (`pos = ttlm->optional`) carries the property one
  hop further, so the leaf `get_ttlm(map_size, pos)` is certified too.

Verdict is the usual `verdict(callers, guarded)`; the shape only emits when
`guarded > 0` (the data-pointer-param precondition is otherwise far too
broad). `caller_verdicts()` in `pipeline_eval.py` now prefers a guarded
verdict when a function matches several shapes — `get_ttlm` also matches the
scalar `len-param` shape (its `bm_size` arg) as UNGUARDED, and the
non-local CALLER-GUARDED proof wins.

**Funnel delta (rc7-db net surface).** Of the 29 genuine-UNGUARDED
skb/cursor survivors, **2 collapse to CALLER-GUARDED** (29 → 27; precond-
resolved 7 → 9). The shape certifies 6 functions, all genuine validate-
then-store parsers — the ttlm trio (`get_ttlm`, `parse_adv_t2l`,
`parse_neg_ttlm`) plus the HE/EHT capability parsers
(`he_cap_ie_to_sta_he_cap`, `verify_peer_he_mcs_support`,
`verify_sta_eht_mcs_support`) — and the previously verbatim-REAL survivor
`ieee80211_get_ttlm` is now correctly resolved without a harness.

* **Soundness caveat** — like the struct-field shape, this is a ranking
  signal, not a proof: it shows the consumed element came from a length-
  validated storage slot, but it does not prove the validator's byte budget
  covers *every* downstream read (for `get_ttlm` the manual triage did that
  separately and exactly). It dependably retires the non-local-validator FP
  *class*; it should not be read as a bound proof for an arbitrary parser.
