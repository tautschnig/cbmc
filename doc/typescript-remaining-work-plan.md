# TypeScript Frontend — Remaining Work Plan (2026-05-29)

For the consolidated tracker of limitations, architectural debt, and
external dependencies, see
[typescript-known-limitations.md](typescript-known-limitations.md).
This document is the prioritised roadmap of planned future work; it
is the answer to "what should we do next?"

Replaces the 2026-05-16 plan; reprioritised after the security-audit
work surfaced new high-leverage items (§4.3 cross-procedural triage,
public CodeQL pack release) and confirmed several smaller items as
lower priority than originally estimated.

## Current state

- 735 CORE tests, 1 KNOWNBUG (`async-race-undetected`, opt-in design choice).
- Frontend invariant violations observed across 301 real-world AWS
  TypeScript harnesses: 1 class, now resolved (§2.9). See
  [typescript-known-limitations.md §7](typescript-known-limitations.md)
  for the audit-driven validation evidence.

## Recently resolved (since 2026-05-16)

| Plan item | Status | Commit |
|-----------|--------|--------|
| 2.8 mixed-union arrays workaround | done | 5c06761f7e |
| 3.1 object-bits auto-detect warning | done | 5c06761f7e |
| 9. WeakRef.deref() | done | 5c06761f7e |
| 2.9 for-of/split member_exprt invariant | done | 1dda2c44f6 |
| 3.4 taint analysis precision (value-typed) | done | (see changelog) |

---

## Priority bands

Priority is `(impact × feasibility) / risk`, weighted by signal from
the 35-repo, 301-harness audit run. P0 = soundness-relevant or
cheap-and-clear; P1 = high impact, well-bounded; P2 = medium impact;
P3 = low impact or high risk; P4 = long-term / external dependency.

### P0 — Soundness-relevant, ≤ 2 days

| # | Item | Estimate | Status |
|---|------|----------|--------|
| **P0.3** | Upstream the 4 solver-side fixes accumulated on this branch | 1–2 days + review cycle | Pending |
| **P0.4-light** | Document the value-typed-proxy null-safety pattern for §1.7 with a worked example regression test | 2 hours | Pending |

P0.1 (mixed-union workaround), P0.2 (object-bits warning), and the
original "P0.4 full nullable wrapper" have been moved out of P0:
the first two are done; the full P0.4 is reprioritised down because
the 35-repo audit did not encounter a single reference-type-null
issue that the current value-typed proxy pattern could not handle.
A primitive (`__CPROVER_nondet_nullable_ref<T>()`) can be added later
if a real user need surfaces.

### P1 — High user-facing impact, well-bounded effort

| # | Item | Estimate |
|---|------|----------|
| **P1.1** | RegExp Phase 2 (metacharacters via NFA: `.` `*` `+` `?` `[]` `^` `$` `\d` `\w` `\s` `\|`) | 2–3 days |
| **P1.2** | Date calendar getters (getMonth, getDate, getDay, getHours, getMinutes, getSeconds, getMilliseconds) | 1 day |
| **P1.3** | Integrate CodeQL `DataFlow::Global` into triage queries | 3–4 days |
| **P1.4** | Public release skeleton for the CodeQL pack (waits on coordinated-disclosure completion) | 2–3 days |

#### P1.1 — RegExp Phase 2 plan

- Build a small NFA from the regex pattern at conversion time (Thompson construction).
- For constant input strings: simulate the NFA and return true/false.
- For symbolic input: return nondet (Phase 3 with SMT `str.in_re` would handle this).
- Supported metacharacters: `.` (any char), `*` (zero+), `+` (one+), `?` (optional), `[abc]` / `[^abc]` (char class), `^` / `$` (anchors), `\d` / `\w` / `\s` (shorthand classes), `|` (alternation), `()` (grouping, no captures).
- Unsupported (defer): backreferences, lookahead/lookbehind, named groups, Unicode property escapes.

**Risk**: Medium (NFA construction is well-understood; regex edge cases are numerous but bounded by the supported subset).
**Dependencies**: None for Phase 2. Phase 3 depends on SMT-string integration (P4.1).

#### P1.2 — Date getters plan

Implement ES2024 §21.4.1 algorithms:
- `getMonth`: extract month from days-since-epoch (needs leap-year table).
- `getDate`: day-of-month from days-since-epoch.
- `getDay`: `(days + 4) % 7` (Jan 1 1970 was Thursday = 4).
- `getHours/Minutes/Seconds/Milliseconds`: modular arithmetic on the time value.

The hours/minutes/seconds/milliseconds are simple
(`Math.floor(time / 3600000) % 24`, etc.). Month and day-of-month
need the cumulative-days-per-month table with leap-year handling.

**Risk**: Low (pure arithmetic, well-specified).

#### P1.3 — CodeQL DataFlow::Global integration

The auto-triage pipeline currently relies on syntactic CodeQL matches
plus a manual cross-procedural pass implemented in `triage.py` (grep
callers + classify args). Replacing this with CodeQL's
`DataFlow::Global` API for each query would:

- Move call-graph reasoning into the query itself (more precise, less
  brittle than grep-based caller analysis).
- Remove the `triage.py` cross-proc heuristic entirely.
- Enable cross-module data flow that the grep approach cannot follow.

**Plan**:
1. Convert each Q1–Q6 query to expose a `Configuration` extending `DataFlow::Global`.
2. Sources: function-parameter access where the function is a public export.
3. Sinks: the dangerous-pattern locations matched today.
4. Sanitisers: known-safe library calls (e.g., `Object.hasOwn`, regex `.test`) modelled in CodeQL.
5. Re-run the audit on a representative subset (5 repos) and compare TP/FP rates against the current pipeline.

**Risk**: Medium — DataFlow::Global has its own learning curve; some queries may need re-modelled sanitisers.

#### P1.4 — Public release of the CodeQL pack

Currently the `aws-ts-anti-patterns/` pack and `triage.py` live outside
this repo. After coordinated disclosure of the 14 findings completes:

1. Move the pack to its own GitHub repo.
2. Add a `README.md` walking through how to add a new database, run the queries, and triage hits.
3. Add a GitHub Actions workflow that runs the pack on a TypeScript repo and posts results as a SARIF upload.
4. Cross-reference from this doc and from `typescript-known-limitations.md §7`.

**Blocked on**: coordinated disclosure window. Skeleton work (the README and Action) can start now.

### P2 — Medium impact, low risk

| # | Item | Estimate |
|---|------|----------|
| P2.1 | Generator `next(value)` parameter | 1–2 days |
| P2.2 | `yield*` delegation (constant case) | half day |
| P2.3 | Well-known Symbols (`Symbol.iterator` etc., as named special properties) | 1 day |
| P2.5 | Dynamic `import()` resolution via existing module infrastructure | half day |
| P2.6 | AST server error message improvements | 1 day |
| P2.7 | Harness template library (3–4 new templates: stack-overflow recursion, allowlist injection at call site, cross-fn taint) | 2–3 days |

(P2.4 WeakRef.deref() landed in 5c06761f7e and is no longer in this list.)

### P3 — Low impact or high risk

| # | Item | Estimate | Risk |
|---|------|----------|------|
| P3.1 | `Object.create` (single-prototype case) | 1 day | medium |
| P3.2 | `Object.setPrototypeOf` runtime chain | 2–3 days | high |
| P3.3 | Proxy handler traps | 5–7 days | high |
| P3.4 | Mixed-union arrays — proper core fix in `simplify_member` (workaround already landed) | 1–2 days | medium |

### P4 — Long-term / external dependency

| # | Item | Notes |
|---|------|-------|
| P4.1 | SMT-string integration (`tautschnig/py` branch) | When it lands, §1.2/§1.3/§1.4/§3.1/RegExp-Phase-3 all improve simultaneously. |
| P4.2 | First-class concurrency (lift `--ts-async-threading` to default) | Only if a real-world JS class needs it; current opt-in flag handles known cases. |

---

## Suggested two-week execution order

**Week 1** — P0 backlog and the highest-impact P1 item:

| Day | Tasks |
|-----|-------|
| 1 | P0.4-light: regression test + doc update for §1.7 workaround. Update work plan (this file) to reflect resolved items. |
| 2–3 | P0.3: review the four solver-side commits, prepare them as standalone upstream-able patches; file as a draft against the public CBMC repo for upstream review. |
| 3–5 | P1.1: RegExp Phase 2 — NFA construction + simulation for constant input; update the `string-regex-test-*` regression family. |

**Week 2** — Remaining P1 items and Phase 1.4 prep:

| Day | Tasks |
|-----|-------|
| 1 | P1.2: Date calendar getters with leap-year table. |
| 2–4 | P1.3: CodeQL DataFlow::Global integration for Q1, Q2, Q4 (the queries that produced the most cross-procedural FPs). |
| 5 | P1.4 prep: README skeleton, GitHub Action draft, repo layout. Public push held until disclosure window opens. |

P2 items can be picked up opportunistically as user needs surface.

---

## Cross-references

- [typescript-known-limitations.md](typescript-known-limitations.md) — full limitation tracker
- [typescript-fixes-changelog.md](typescript-fixes-changelog.md) — historical fixes
- [refined-string-migration-plan.md](refined-string-migration-plan.md) — long-term string-model migration
- [over-approximation-audit.md](over-approximation-audit.md) — sound over-approximations
