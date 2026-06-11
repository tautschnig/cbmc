# Per-subsystem scoping harness (2026-06)

**Date:** 2026-06-11. Motivation: after reaching 88.5% subsystem coverage
(`subsystem_census.py`) and adding the validate-at-storage caller-precondition
shape, two analyses hit a scaling wall on the whole-class DBs:

* the interprocedural-taint finders **time out / error** on whole-`drivers/`
  (QUERY-TIMEOUT at 360 s even at 200 GB) — so `drivers/*` was *covered* but
  produced **zero verdicts**;
* `caller_precondition.ql` takes **~19 min** on the whole-6.12-`net` DB
  (its SSA / dominance / forall-over-calls global relations).

So coverage outran adjudication. This harness scopes analysis to a single
leaf subsystem so the finders and caller_precondition run in minutes.

## Two scoping mechanisms (and which actually works)

### 1. Query-level scope — `Scope.qll` + `ABC_SCOPE_PREFIX`

`Scope.qll` declares an external predicate `scopePrefix(string)` and a
`predicate inScope(Element)` that is universally true when no prefix is
supplied. Every finder + caller_precondition now gates its flagged candidate
on `inScope(...)` as the first `where` conjunct. The query runners
(`triage_loop.run_oracle`, `outcome_summary.run_query`) always pass
`--external=scopePrefix=<csv>`, written from `ABC_SCOPE_PREFIX` (a
kernel-relative path prefix, e.g. `drivers/usb/`); empty ⇒ empty relation ⇒
unscoped, an exact no-op (verified: caller_precondition unscoped reproduces
the same 6 validate-at-storage CALLER-GUARDED functions; `tainted_count`
unscoped reproduces the same raw counts).

**What it does and does NOT do.** Gating the candidate prunes the
per-candidate join and the result volume — so on a *moderate* shared DB it
helps a lot (`tainted_count` scoped to `drivers/usb/` on the next-`drivers/`
DB: **60 s**, vs >360 s timeout DB-wide). But it does **not** prune the
global helper relations (`structCarriesRawBytes`, the SSA/dominance graph,
`forall` over all call sites), which CodeQL materialises DB-wide regardless.
So on the *giant* whole-6.12 DBs it does not help: `tainted_count` scoped to
`drivers/usb/` on the 36 GB 6.12-`drivers/` DB still times out at 10 min, and
caller_precondition scoped to `net/mac80211/` on 6.12-`net` still times out
at 10 min. Query-scope is a real but partial lever.

### 2. Per-leaf DB — `scope_eval.sh` (the dependable mechanism)

When the DB *is* the leaf, **every** base relation is cheap, so all 8
finders + caller_precondition run fast — this is what dependably unblocks the
heavy cases. `scope_eval.sh <tree> <leaf>...` builds a per-leaf CodeQL DB
(cached, with BUILD-TIMEOUT tracking) and runs the outcome aggregator over
it. A per-leaf DB builds in well under a minute for typical leaves
(drivers/nfc: 41 s, 45 .c files) and the full 8-finder suite then completes
with no timeouts.

## Drivers verdicts — previously unobtainable

Sweep of four attacker-facing `drivers/*` leaves (6.12), build + 8 finders
each, ~11 min total, **32/32 finder runs completed, 0 timeouts, 0 errors**:

| asset | raw | mitigated | genuine |
|-------|----:|----------:|--------:|
| A1-mem count/index | 91 | 0 | 91 |
| A1-mem decoded-len | 3 | 0 | 3 |
| A1-mem skb/cursor | 226 | 0 | 226 |
| A1-UB div/shift | 8 | 1 | 7 |
| A2 confidentiality | 61 | 5 | 56 |
| A3 integrity/CFI | 4 | 0 | 4 |
| A4 availability | 19 | 1 | 18 |
| A5 authorization | 0 | 0 | 0 |
| **TOTAL** | **412** | **7** | **405** |

Per-leaf: drivers/nfc 136, drivers/bluetooth ~128 (74 skb/cursor, 36 A2),
drivers/hid, drivers/firewire 28. These are the first concrete `drivers/*`
candidate counts the pipeline has produced — the whole-`drivers/` DB returned
none (every taint finder timed out or errored).

## Honest status

* The per-leaf-DB harness is the dependable path; the query-scope is a
  complementary lever for moderate shared DBs and for attributing results to
  a leaf without a rebuild. Both are kept; the no-op default preserves every
  existing whole-DB workflow.
* This produces **candidate** counts per leaf (the finder layer). The genuine
  residual still feeds the CBMC discharge / caller-precondition stages; the
  drivers genuine set is large (405) and untriaged — scoping made it
  *reachable*, not *resolved*.
* Building a per-leaf DB for all 127 6.12 driver leaves is a batch job
  (~1 min each); this run demonstrates the mechanism on four leaves.
