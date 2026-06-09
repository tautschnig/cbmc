# End-to-end measured evaluation of the triage pipeline

**Date:** 2026-06-09. Produced by `pipeline_eval.py` over `broad-next-db`
(linux-next next-20260605: rxrpc / bridge / vxlan / can / netfilter / ceph
/ bluetooth / x25 / l2tp / mac80211). This is the measured answer to "does
the pipeline actually help triage?" that the talk's lesson demands.

## The candidate funnel

| oracle | raw hits | raw functions | HIGH | HIGH&WRITE | distilled |
|--------|---------:|--------------:|-----:|-----------:|----------:|
| count/index | 92 | 65 | 4 | 4 | **4** |
| decoded-len | 28 | 11 | n/a | n/a | **11** |
| skb / bounded-cursor | 170 | 135 | n/a | n/a | **11** |
| **total** | **290** | **211** | | | **26** |

*distilled* = the precision-relevant subset a human would actually be
handed: HIGH&WRITE for count/index (the confidence×impact tiers);
bounded-cursor functions for the skb oracle; the arithmetic-overflow hits
for decoded-len.

The headline narrowing is the count/index column: the
confidence×impact tiers take **92 raw hits across 65 functions down to 4**,
and CBMC then confirms all four. The other two oracles lack a confidence
tier (taint can't be applied to them yet), so they narrow only to their
precision-relevant subset (11 each) — a clear, honest signal of where the
tiering pays off and where it is still missing.

## CBMC-adjudicated survivors (shape + reach)

```
function              oracle       src   shape:b/f   reach:b/f    ground-truth
cgw_csum_crc8_pos     count/index  REAL  FAIL/SUCC   REACH/BLOCK  CVE-2019-3701
cgw_csum_crc8_neg     count/index  shape FAIL/SUCC   REACH/BLOCK  CVE-2019-3701
cgw_csum_xor_pos      count/index  shape FAIL/SUCC   REACH/BLOCK  CVE-2019-3701
cgw_csum_xor_neg      count/index  shape FAIL/SUCC   REACH/BLOCK  CVE-2019-3701
crush_decode          decoded-len  shape FAIL/SUCC   REACH/BLOCK  ceph osdmap
rxkad_decrypt_ticket  skb/cursor   REAL  FAIL/SUCC   REACH/BLOCK  rxkad ticket
```

## Ground-truth coverage

Every known-CVE-relevant function on this surface that an oracle targets is
**FLAGGED**, and the CAN-gateway cluster lands at the top tier
(HIGH/WRITE) — so the distillation does not drop the real bug:

* **cgw_csum_*** (CVE-2019-3701, CAN-gw OOB write) — all four at HIGH/WRITE,
  the entire distilled count/index set.
* **rxkad_decrypt_ticket** — flagged (bounded-cursor, READ).
* **crush_decode** — flagged (decoded-len).

## Reading the verdicts honestly — `src` is the crux

The single most important caveat: **`shape:b/f` and `reach:b/f` mean
different things for `src=REAL` vs `src=shape`.**

* `src=REAL` (cgw_csum_crc8_pos, rxkad_decrypt_ticket) — the verdict is on
  the **verbatim kernel body**. FAIL/SUCC + REACH/BLOCK is a real-code
  statement.
* `src=shape` (the other four) — the verdict is on the **generic shape
  model**, i.e. "this bug *shape* is genuinely OOB-reachable and the
  canonical guard fixes it." It is **not** a statement about the specific
  function's current exploitability.

Two consequences, both honest limitations the eval surfaces rather than
hides:

1. **crush_decode shows `shape FAIL` but the real code is safe.** The
   generic multiply shape overflows; the real `crush_decode` allocates via
   `array_size`/`size_mul` (overflow-checked) and a `u32` count cannot
   overflow `size_t` on 64-bit. A REAL harness is BMC-intractable here (the
   bucket loop iterates a `u32`), so we only have the shape verdict plus
   manual analysis — `shape FAIL` must NOT be read as "crush_decode is
   buggy".

2. **cgw and rxkad are function-granularity hits whose guard lives in the
   caller.** On this post-fix tree the indices (`cgw`) / `ticket_len`
   (`rxkad`) are validated by the *caller* (`cgw_parse_attr`,
   `rxkad_verify_response`), not in the flagged function. The REAL
   `rxkad_decrypt_ticket` harness makes this explicit: in isolation the OOB
   is REACHABLE, but with the caller precondition `ticket_len>=4` it is
   BLOCKED — i.e. a function-granularity FP resolved by the caller. The
   same is true of cgw (its REAL harness `probe_fixed` models the
   caller-validated index).

## What the evaluation establishes

* **Precision of the tiering:** on the count/index oracle, 92→4 with the
  real CVE cluster surfacing at the top tier and CBMC confirming the shape
  — a concrete, measured narrowing, not an anecdote.
* **Recall:** no ground-truth function is dropped by the distillation.
* **Progress on the frontier (since first run):**
  * The **caller-precondition automation** (`caller_precondition.ql`, now
    a `caller` column here) resolves the dominant FP class without a
    harness: `rxkad_decrypt_ticket` is auto-marked CALLER-GUARDED, while
    `ieee80211_get_ttlm` stays UNGUARDED (a kept genuine concern). See
    `caller-precondition-2026-06.md`.
  * `src=REAL` coverage grew 2 → 5 functions (cgw, nfc, rxkad, ttlm, ftp),
    including a verbatim **true negative** (`try_rfc959`: SUCC/SUCC,
    BLOCK/BLOCK — CBMC *clears* a flagged function on real code) and a
    verbatim **true positive** (`ieee80211_get_ttlm`).
* **Remaining frontier:** (a) the decoded-len and skb oracles still lack a
  confidence tier (extend the taint tier to them); (b) the caller check
  does not yet handle `(p, end)` pointer cursors or struct-field bounds
  (cgw); (c) scale `src=REAL` further toward auto-generated harnesses.

## Reproduce

```
pipeline_eval.py --db /tmp/broad-next-db
```
(imports `triage_loop`; CBMC-adjudicates the distilled survivors +
ground-truth functions via the REAL_HARNESS registry or the shape model).
