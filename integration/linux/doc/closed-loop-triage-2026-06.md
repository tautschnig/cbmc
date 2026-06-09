# Closed-loop triage: oracle hit → CBMC verdict, with confidence × impact

**Date:** 2026-06-09. Implements next-steps #1 (close the stage-1→stage-2
loop) and #2 (exploitability/impact axis) from
`talk-inspired-ideas-2026-06.md`.

## Motivation

The "kernel security in the age of AI" talk's central defender message:
*finding* candidate bugs is now cheap (everyone has LLM scanners); the
bottleneck is **triage** — deciding which candidates are real,
exploitable, and urgent — and LLMs systematically overstate impact (claim
LPE, deliver DoS). Our pipeline's differentiator is the stage-2 CBMC
layer, which produces a machine-checked verdict rather than a narrative.
This work turns a flat list of oracle hits into a ranked, adjudicated
triage table.

## Two triage dimensions, surfaced on every candidate

1. **Confidence (precision)** — HIGH when the count/index is
   raw-taint-reachable (`KernelTaintFlow::isRawTaintReachable`), else
   MEDIUM. Added in the taint Phase 2+3 work.
2. **Impact (exploitability)** — WRITE (high: an OOB *write*, the
   primitive that escalates to control-flow / data corruption) vs READ
   (lower: OOB *read* / info-leak / DoS). For `count-loop-write` the
   access is always a write; for `direct-index` it is WRITE iff the
   subscript is an assignment lvalue, else READ. `skb_field` /
   bounded-cursor hits are READ.

## The loop — `triage_loop.py`

```
CodeQL oracle (tiered)  ->  parse confidence+impact  ->
  oracle_harness_gen.py (harness parameterised by candidate array size) ->
    CBMC on harness_buggy + harness_fixed  ->  triage table
```

Output, sorted by confidence then write-impact:

```
function | kind | conf | impact | CBMC:buggy | CBMC:fixed
```

A `FAILED / SUCCESSFUL` pair means CBMC confirmed the bug shape is
genuinely OOB-reachable *and* that the canonical guard removes it.
Supports all three shape oracles (count / decoded / skb-bounded-cursor)
and any CodeQL DB; generalises the previously vme_user-only `pipeline.sh`.

```
triage_loop.py --db <codeql-db> --query <oracle.ql> \
    [--min-confidence HIGH] [--limit N]
```

## Validation

**broad-next-db (linux-next 7.x), `tainted_count_into_fixed_array.ql`,
`--min-confidence HIGH`:**

```
function            kind          conf  impact  CBMC:buggy  CBMC:fixed
cgw_csum_crc8_pos   direct-index  HIGH  WRITE   FAILED      SUCCESSFUL
cgw_csum_crc8_neg   direct-index  HIGH  WRITE   FAILED      SUCCESSFUL
cgw_csum_xor_pos    direct-index  HIGH  WRITE   FAILED      SUCCESSFUL
cgw_csum_xor_neg    direct-index  HIGH  WRITE   FAILED      SUCCESSFUL
```

The four `cgw_csum_*` candidates — the genuine CAN-gateway OOB-write
primitive (`cf->data[crc8->result_idx] = ...`, `result_idx` raw from a
netlink `nla_memcpy`) — surface as HIGH × WRITE with a machine-checked
FAILED/SUCCESSFUL. Exactly the prioritised, evidence-backed shortlist the
talk says defenders need.

**synthetic `count_db`:** `rx_key_buggy` HIGH/READ, `flood_cfg_buggy`
MEDIUM/WRITE — both FAILED/SUCCESSFUL.
**`declen_db` / `bc_db`:** decoded-length and bounded-cursor candidates
also resolve FAILED/SUCCESSFUL through the same loop.

## Honest scope and next step

The harness models the bug **shape** parameterised by the candidate's
extracted constants (array size, kind), not the verbatim function body.
A FAILED/SUCCESSFUL pair confirms the shape is OOB-reachable and that the
canonical guard fixes it; it does **not** prove the specific call site is
reachable with attacker input. Closing that last gap is the
real-function-harness path (`auto_harness.py`, which reads the actual
body) and the **CBMC cover-probe "assumption bisection"** idea
(`talk-inspired-ideas-2026-06.md` #2): auto-insert `__CPROVER_cover`
probes along a claimed path and report the first infeasible hop. That is
the natural follow-on now that the loop and triage axes are in place.

## Files

* `triage_loop.py` — the orchestrator.
* `oracle_harness_gen.py` — shape harness generator (existing; reused).
* `tainted_count_into_fixed_array.ql`, `skb_field_before_lencheck.ql` —
  now emit `impact=`.
