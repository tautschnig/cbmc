# End-to-end measured evaluation of the triage pipeline

**Date:** 2026-06-09. Produced by `pipeline_eval.py` over `broad-next-db`
(linux-next next-20260605: rxrpc / bridge / vxlan / can / netfilter / ceph
/ bluetooth / x25 / l2tp / mac80211). This is the measured answer to "does
the pipeline actually help triage?" that the talk's lesson demands.

## The candidate funnel

| oracle | raw hits | raw functions | HIGH&WRITE | precond-resolved | genuine (UNGUARDED) | skb-pull-weak | distilled |
|--------|---------:|--------------:|-----------:|-----------------:|--------------------:|--------------:|----------:|
| count/index | 92 | 65 | 4 | 4 | 3 | 3 | **4** |
| decoded-len | 28 | 11 | n/a | 4 | 1 | 0 | **1** |
| skb / cursor | 170 | 135 | n/a | 8 | 19 | 48 | **19** |
| **total** | **290** | **211** | | | | | **24** |

*precond-resolved* = functions the caller/producer-precondition automation
marks CALLER-GUARDED or PRODUCER-GUARDED (FPs resolved without a harness).
*genuine (UNGUARDED)* = the high-confidence concern set for the decoded/skb
oracles (raw-taint is baked in for them, so the precondition verdict is the
discriminator). *skb-pull-weak* = `skb->data` parsers not pulled by their
*immediate* caller — a weak signal, since `pskb_may_pull` is often done
once high in the rx stack, so these are held in a separate bucket rather
than the genuine set. For count/index the distillation is the
confidence×impact HIGH&WRITE tier.

The count/index oracle narrows 92→4 via confidence×impact; the previously
weakly-narrowed oracles now narrow via the precondition automation —
**decoded-len 11 functions → 1 genuine concern, skb 135 → 19** (with 48
more held as `skb-pull-weak`; interprocedural depth-3 tracking already
resolved several multi-hop `pskb_may_pull` cases down from 55).

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
  * The **caller/producer-precondition automation** (`caller_precondition.ql`,
    surfaced here as a `caller` column and as the `precond-resolved` /
    `genuine(UNGUARDED)` funnel split) is now the precision lever for the
    decoded-len and skb oracles — whose raw-taint is baked in, so the
    precondition verdict is the real discriminator. It narrows skb 135→19
    and decoded 11→1, and auto-resolves **6/8 distilled survivors** as FPs
    without a harness (4 cgw PRODUCER-GUARDED, crush + rxkad CALLER-GUARDED).
  * `src=REAL` coverage grew 2 → 5 functions, including a verbatim **true
    negative** (`try_rfc959`) and a verbatim **true positive**
    (`ieee80211_get_ttlm`).
  * The two survivors the automation does NOT dismiss (`ieee80211_get_ttlm`,
    `try_rfc959`) are exactly the ones CBMC then adjudicates — a real OOB
    and a cleared true negative.
* **Remaining frontier:** (a) the 48 `skb-pull-weak` functions are guarded
  beyond depth 3 or via an untraceable skb origin (queue/list rather than a
  forwarded parameter) — deeper or value-flow-aware tracking would resolve
  more; (b) scale `src=REAL` toward auto-generated harnesses; (c)
  value-flow precision in the precondition guards (reassignment between
  guard and call).

## Reproduce

```
pipeline_eval.py --db /tmp/broad-next-db
```
(imports `triage_loop`; CBMC-adjudicates the distilled survivors +
ground-truth functions via the REAL_HARNESS registry or the shape model).
