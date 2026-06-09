# Cover-probe "assumption bisection"

**Date:** 2026-06-09. Implements idea #2 from
`talk-inspired-ideas-2026-06.md` (the talk's "BPF-checkpoint" trick), as a
CBMC stage-2 tool.

## What it does

Given a CLAIMED path A→B→C→D to a sink — the kind of narrative an oracle or
an LLM produces ("this is reachable when type==X, then len>N, then we hit
the copy") — it validates each hop with a CBMC coverage probe carrying the
assumption made there. The first infeasible hop is exactly where the
claimed reasoning diverges from what the code allows.

This upgrades a stage-2 verdict from **"the bug shape is reachable"** (the
closed-loop bounds-check question, `triage_loop.py`) to **"this specific
precondition chain to the sink is / is not feasible"** — which is the
defender's actual question: *does this CVE manifest, and does the fix close
the path?*

## Mechanism

CBMC supports `__CPROVER_cover(cond)` with `--cover cover`: each becomes a
goal reported SATISFIED (the point is reachable AND `cond` is feasible
there) or FAILED. `cover_probe.h` wraps this as `CHECKPOINT(id, cond)` /
`REACH(id)`, switched on by `-DCBMC_PROBE` (no-ops for a normal compiler,
so instrumented files still build). `cover_probe.py` runs CBMC, filters
goals to the probe-bearing function (`--probe-function` when the CBMC entry
differs from the callee that holds the probes), prints the hops in path
order, flags the first BLOCKED one, and exits 0 (feasible) / 1
(infeasible).

```
cover_probe.py --file F.c --function ENTRY \
    [--probe-function CALLEE] [--unwind N]
```

## Validation

**Synthetic (`cover_bisect_test.c`):**
- `parse_claimed_overflow` — a claimed overflow whose `len>100` hop is
  ruled out by an earlier `len<=64` clamp: the probe at that hop is
  BLOCKED and the driver pinpoints it as where the claim breaks.
- `parse_real_overflow` — no clamp: every hop feasible, "reachable as
  described".

**Real CVE-2026-31622 (`real_nfc_llcp_cover.c`):** the OOB-read
precondition `offset + 2 + length > tlv_array_len` at the value-read site
is

```
vulnerable parser  ->  [reached]  precondition REACHABLE   (the CVE path exists)
fixed parser       ->  [BLOCKED]  precondition INFEASIBLE  (the length guard closes it)
```

— a machine-checked answer to "does this CVE manifest, and does the fix
close it?".

## Where it sits in the pipeline

```
stage-1 CodeQL oracle (confidence x impact tiers)
   -> triage_loop.py        : is the bug SHAPE OOB-reachable?           (bounds-check)
   -> cover_probe.py         : is the claimed PRECONDITION CHAIN feasible? (reachability)
```

The two are complementary: the loop ranks and confirms the shape; the
cover-probe adjudicates a specific claimed path / whether a guard closes
it. Both reuse the same goto-cc/CBMC infrastructure.

### Unified output (integrated)

`triage_loop.py` now runs BOTH per candidate in one pass.  The generated
harnesses (`oracle_harness_gen.py`) carry an OOB-precondition CHECKPOINT
at the sink, so the table is:

```
function | conf | impact | shape:bug | shape:fix | reach:bug | reach:fix
```

* **shape** — CBMC bounds/overflow verdict (FAILED = the bug shape is
  present).
* **reach** — cover-probe on the OOB precondition (REACHABLE = the
  precondition is feasible at the sink; BLOCKED = the canonical guard
  closes the path).

broad-next-db HIGH tier:

```
cgw_csum_crc8_pos  HIGH  WRITE  shape:FAILED  shape:SUCCESSFUL  reach:REACHABLE  reach:BLOCKED
... (all four cgw_csum_*)
```

The two axes reinforce each other: the bounds check proves the shape is
buggy; the cover-probe independently confirms the OOB precondition is
reachable in the vulnerable shape and that the guard closes it in the
fixed shape.

## Files

* `cover_probe.h`, `cover_probe.py` — the tool.
* `cover_bisect_test.c` — synthetic feasible/infeasible fixture.
* `real_nfc_llcp_cover.c` — real CVE-2026-31622 vulnerable-vs-fixed demo.
