# TypeScript Frontend — Remaining Work Plan (2026-05-31)

For the consolidated tracker of limitations, architectural debt, and
external dependencies, see
[typescript-known-limitations.md](typescript-known-limitations.md).
This document is the prioritised roadmap of planned future work; it
is the answer to "what should we do next?"

## Current state

- 770+ CORE tests, 1 KNOWNBUG (`async-race-undetected`, opt-in design choice).
- Frontend invariant violations observed across the 35-repo, 75-finding
  audit run: 1 class, now resolved (§2.9 in the limitations doc). See
  [typescript-known-limitations.md §7](typescript-known-limitations.md)
  for the audit-driven validation evidence.
- All P2 and P3 items from the 2026-05-29 plan have been resolved
  (see Completed appendix below).

---

## Active items (priority order)

### P0 — Soundness-relevant

| # | Item | Estimate | Status |
|---|------|----------|--------|
| **P0.3** | Upstream the 4 solver-side fixes accumulated on this branch | 1–2 days + review cycle | Pending push permission |

The four commits live on `~/upstream-prep-cbmc-strings/` ready to
push. They benefit any frontend (not just TypeScript) and are
unblocked once permission is granted.

### P1 — High user-facing impact

| # | Item | Estimate | Status |
|---|------|----------|--------|
| **P1.1b** | RegExp Phase 2b: `\|` alternation, `(...)` grouping, `{n,m}` quantifiers | 2 days | Held by user request |
| **P1.4** | Public release skeleton for the CodeQL pack | 2–3 days | Blocked on coordinated-disclosure window |

#### P1.1b — RegExp Phase 2b plan

Extend the NFA built in Phase 2 (commit `7d98efe271`) to handle
alternation, grouping, and bounded quantifiers. The NFA construction
already supports the underlying structure; this is mostly parser
work plus a few simulation extensions.

**Risk**: Medium (regex edge cases are numerous; new test coverage
needed for backtracking-prone patterns).

**Dependencies**: None for Phase 2b. Phase 3 (symbolic input
matching via SMT `str.in_re`) depends on SMT-string integration
(P4.1).

#### P1.4 — Public release of the CodeQL pack

After coordinated disclosure of the 14 findings completes:

1. Move the `aws-ts-anti-patterns/` pack to its own GitHub repo.
2. Add a `README.md` walking through database creation, query
   running, and triage.
3. Add a GitHub Actions workflow that runs the pack on a TypeScript
   repo and posts results as a SARIF upload.
4. Cross-reference from this doc and from
   `typescript-known-limitations.md §7`.

Skeleton work (the README and Action) can start now; the public
push is held until the disclosure window opens.

### P2 — Medium impact (deferred)

| # | Item | Estimate | Status |
|---|------|----------|--------|
| P2.1 | Generator `next(value)` parameter | 2–3 days | Deferred — needs state-machine refactor; audit doesn't exercise it |

### P3 — Lower impact / harder upgrades (deferred)

The "pragmatic" implementations for P3.1–P3.4 cover the audit-relevant
cases. The full versions remain documented as future work but no
real harness has blocked on them:

| # | Full-version item | Estimate | Why deferred |
|---|-------------------|----------|--------------|
| P3.2-full | Runtime prototype-chain walking (every property access dispatches through `__proto`) | 2–3 days, high risk | Audit doesn't exercise; common idioms covered by current model |
| P3.3-full | All 13 Proxy traps + element access + dynamic-handler patterns | 5–7 days, high risk | get/set dispatch covers the security-boundary use cases |

### P4 — Long-term / external dependency

| # | Item | Notes |
|---|------|-------|
| P4.1 | SMT-string integration (`tautschnig/py` branch) | When it lands, §1.2/§1.3/§1.4/§3.1/RegExp-Phase-3 all improve simultaneously. |
| P4.2 | First-class concurrency (lift `--ts-async-threading` to default) | Only if a real-world JS class needs it; current opt-in flag handles known cases. |

---

## Suggested execution order

With most pending items either gated on external decisions or
deliberately deferred, the natural next steps are:

1. **P0.3 push** when permission is granted.
2. **P1.4 skeleton work** (README + GitHub Action) in parallel
   with the disclosure window.
3. **P1.1b** when the user lifts the hold on RegExp work.
4. Watch the upstream `tautschnig/py` branch for SMT-string
   landing; **P4.1 follow-up** is 1–2 weeks of migration work
   when it lands.

P2.1 and the P3.x full-version upgrades are picked up only if a
real harness blocks on them.

---

## Completed (chronological appendix)

Items resolved in earlier work, kept here for traceability. See
[typescript-fixes-changelog.md](typescript-fixes-changelog.md) for
the full narrative on each.

### Since 2026-05-29

| Item | Commit |
|------|--------|
| P3.3 Proxy get/set trap dispatch (inline-handler scope) | `06338955e3` |
| P3.2 setPrototypeOf common-idiom recognition | `cc199cab3a` |
| P3.4 Mixed-element-type array literals — defensive simplifier guard + frontend bailout | `0ca776b136` |
| P3.1 `Object.create` static-prototype case | `3bd37894a6` |
| P2.2 `yield*` delegation (constant case) | `834be36883` |
| P2.6 Surface TypeScript syntax errors with file:line:col | `139eea8858` |
| P2.7 Harness template library (recursion-DoS, allowlist-injection, prototype-key-injection, path-traversal) | `d45a286d88` |
| P2.3 Well-known Symbols as `@@<name>` special property names | `17c007d28f` |
| P2.5 Dynamic `import()` resolution | `24e8048a9d` |
| P1.3 CodeQL `DataFlow::Global` integration (Q1, Q2, Q4) | `27e19b1795` |
| P1.2 Date calendar getters | `532a56424c` |
| P1.1 RegExp Phase 2 (NFA-based metacharacters) | `7d98efe271` |

### 2026-05-16 to 2026-05-28

| Item | Commit |
|------|--------|
| 2.8 mixed-union arrays workaround | `5c06761f7e` |
| 3.1 object-bits auto-detect warning | `5c06761f7e` |
| 9. WeakRef.deref() | `5c06761f7e` |
| 2.9 for-of/split member_exprt invariant | `1dda2c44f6` |
| 3.4 Taint analysis precision (value-typed) | (see changelog) |
| Spec cross-referencing fixes (~30 small bug classes) | (see changelog top section) |

---

## Cross-references

- [typescript-known-limitations.md](typescript-known-limitations.md) — full limitation tracker
- [typescript-fixes-changelog.md](typescript-fixes-changelog.md) — historical fixes
- [refined-string-migration-plan.md](refined-string-migration-plan.md) — long-term string-model migration
- [over-approximation-audit.md](over-approximation-audit.md) — sound over-approximations
