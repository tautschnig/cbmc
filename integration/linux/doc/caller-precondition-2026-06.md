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

## Honest limits

* **`cgw_csum`** indices are *struct fields* (`crc8->result_idx`), not a
  call argument, so the parameter-level caller check is `no-len-param`
  there; its validation site is the netlink parse (`cgw_parse_attr`), a
  different (field-level) precondition problem — the remaining shape the
  check does not yet cover.
* **Cursor CALLER-GUARDED is a heuristic prior, not a proof** — it shows
  the caller pre-checks the first reads, not that a multi-field sub-decode
  stays in bounds. The UNGUARDED cursor verdict is the dependable one.
* The guard's reject branch is required to be an exit (return/goto/break)
  and the guard condition must strictly dominate the call; what is *not*
  modelled is reassignment of the bound variable between guard and call
  (rare in these idioms) — a value-flow check would close that.
