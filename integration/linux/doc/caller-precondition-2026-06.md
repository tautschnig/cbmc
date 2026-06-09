# Caller-precondition automation

**Date:** 2026-06-09. Automates the dominant residual false-positive class
identified by the end-to-end evaluation: function-granularity oracle hits
whose bound is validated not in the flagged function but in its **callers**.

## The query — `caller_precondition.ql`

For a function with a **bound parameter** (a `len`/`size`/`count`-named
integral; the `(p, end)` pointer-cursor case is a known gap, see below),
it inspects every call site and asks whether the bound argument is
constrained by a *validate-then-reject* guard in the caller:

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
idiom) with line-order as a dominance proxy — robust and fast, no
IR-`GuardCondition` machinery.

## Validation (broad-next-db, net/)

* **`rxkad_decrypt_ticket` → CALLER-GUARDED** (callers=1, guarded=1) —
  reproduces, automatically, the manual finding that the OOB flags-read is
  closed by `rxkad_verify_response`'s `ticket_len >= 4` check (rxkad.c:1167).
* Distribution over the surface: **30 CALLER-GUARDED, 39 PARTIAL, 802
  UNGUARDED**.
* Cross-referenced against the 12 bounded-cursor oracle hits: rxkad is the
  one CALLER-GUARDED (auto-dismissed FP); 5 are UNGUARDED genuine concerns
  (`br_send_bpdu`, `ieee80211_get_ttlm`, `ieee80211_key_alloc`,
  `nf_nat_ipv4/6_csum_recalc`, `nft_payload_n2h`); 5 are `(p,end)`
  pointer-cursors the bound model does not yet match.

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

## Honest limits

* **`(p, end)` pointer cursors** (the ceph/XDR style) have no integral
  length parameter, so `boundParam` does not match them — 5 of the 12
  bounded-cursor hits are `no-len-param`. Extending the model to
  end-pointer guards (`if (p + n > end) ...`) is the natural follow-on.
* **`cgw_csum`** indices are *struct fields* (`crc8->result_idx`), not a
  call argument, so the parameter-level caller check is `no-len-param`
  there; its validation site is the netlink parse (`cgw_parse_attr`), a
  different (field-level) precondition problem.
* The guard model uses line-order as a dominance proxy; it can in
  principle over-credit a guard that does not actually dominate the call.
  A precise dominator check (IR `GuardCondition`) would tighten this.
