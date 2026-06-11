# Collector architecture: soundness boundary (2026-06)

**Date:** 2026-06-11. The governing principle for the threat-finder pipeline.

## Principle

> The pipeline is a **sound, over-approximate COLLECTOR** that identifies all
> candidates matching a threat pattern.  A candidate may be **removed** only
> by a **definitive, fully-sound filter** -- CBMC, a provably-sound static
> check, or manual code review.  Every other signal (taint confidence, an
> in-function relational guard, a compile-time mask, caller-precondition
> verdicts, mitigation dominance, framework-validation) is **ADVISORY**: it
> may rank, tier, or annotate a candidate, but **must never drop one**.

Rationale: for a bug finder, a false negative (a dropped real bug) is far
worse than a false positive.  Heuristic FP-reducers are unsound (existential,
no dominance, instance-insensitive, bound-vs-size unchecked -- see
`drivers-census-2026-06.md` for the "anywhere-in-the-program" analysis), so
they must not gate the candidate set.  Soundness is recovered downstream by
the CBMC discharge stage, not by the CodeQL filter.

## What counts as a SOUND filter (may remove)

* **CBMC discharge** -- the verbatim-body / caller-precondition harness
  witnesses the bug or proves it unreachable.  Definitive.
* **Dominance-based intra-procedural guards** -- a guard / capability check
  that provably DOMINATES the use on every path (e.g.
  `auth_missing_capable`'s `not exists(capability check dominating sink)`:
  if a check dominates, the sink is guarded on all paths -- sound).
* **Threat-spec scoping** -- restricting to the pattern being modelled is
  the collector's specification, not a safety filter: a compile-time-constant
  divisor/size is provably not the attacker-controlled-UB/availability threat
  (`tainted_ub_arith` / `av_unbounded` `not exists(e.getValue())`), and a
  value `v = x & C` provably satisfies `v <= C` (a mathematical fact).  These
  remove non-candidates, not candidates.
* **Manual code review** -- a recorded human soundness argument (e.g. the
  `ieee80211_get_ttlm` byte-budget proof, the CEC `num_log_addrs` triage).

## What is ADVISORY (must never remove)

* taint **confidence** (HIGH/MEDIUM) -- already a tier, not a drop.
* an in-function **relational guard** against a const that is NOT
  dominance-checked (existential -> unsound).
* a compile-time **mask** whose constant-vs-array-size relation is unchecked
  (`v <= C` is sound, but `C < arraySize` is not verified -> unsound as an
  array-safety clear).
* **bare-parameter** / caller-responsibility (the caller may not bound it).
* **caller-precondition** verdicts (CALLER-GUARDED / validate-at-storage) --
  existential, instance-insensitive ("sound enough to rank, not to prove").
* **mitigation dominance** (memset/clamp) and **framework-validation**
  (V4L2 num_planes, CEC num_log_addrs) -- non-local, unproven for the use.

## Compliance status

* `tainted_count_into_fixed_array.ql` -- ALIGNED.  The guard / mask /
  bare-parameter / enum exclusions are now emitted as `adv_guard`,
  `adv_mask`, `adv_bound`, `adv_enum` fields; every count-loop-write and
  fixed-array subscript matching the pattern is retained (misc leaf: 1 -> 62
  candidates; `altera_execute` kept as HIGH/WRITE with `adv_mask=MASKED`).
* `skb_field_before_lencheck.ql` -- ALIGNED.  `hasLengthGuard` and
  `trivialAccessor` are now `adv_lenguard` / `adv_trivial`.
* `tainted_ub_arith.ql`, `av_unbounded.ql` -- compliant (constant scoping is
  sound; mitigation is emitted as an advisory field, not a drop).
* `auth_missing_capable.ql` -- compliant (dominance-based, sound).
* **Consumers** (`pipeline_eval`, `threat_model_eval`, `outcome_summary`,
  `drivers_census`) must treat advisory fields as RANKING only.  They report
  mitigated-vs-genuine as tiers and never delete; `pipeline_eval`'s
  "precond-resolved" is an advisory label on a retained candidate, not a
  removal.  The census candidate totals therefore rise (the collector is now
  over-approximate); that is the intended, sound behaviour.

## Implication for triage

A "triage shortlist" is a RANKING over the full candidate set (e.g.
`confidence=HIGH & impact=WRITE & adv_guard=UNGUARDED & adv_mask=none`), not
a filtered subset.  The advisory fields order the work; CBMC / review close
each candidate.  No candidate leaves the set without a sound verdict.
