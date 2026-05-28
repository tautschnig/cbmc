# TypeScript Frontend — Remaining Work Plan (2026-05-16)

For the consolidated tracker of limitations, architectural debt, and
external dependencies, see
[typescript-known-limitations.md](typescript-known-limitations.md).
This document is the prioritized roadmap of planned future work; it
is the answer to "what should we do next?"

## Current state

- 700 CORE tests, 1 KNOWNBUG (async-race-undetected, opt-in design choice)
- All Track A/B/C items from the previous plan addressed

---

## Documented gaps

### 1. RegExp metacharacters (Phase 2)

**What**: `/a.b/.test(s)` treats `.` as literal; `*`, `+`, `?`, `[]`, `^`, `$`, `\d`, `\w`, `\s`, `|` not supported.

**Plan**:
- Build a small NFA from the regex pattern at conversion time (standard Thompson construction).
- For constant input strings: simulate the NFA and return true/false.
- For symbolic input: return nondet (Phase 3 with SMT `str.in_re` would handle this).
- Supported metacharacters: `.` (any char), `*` (zero+), `+` (one+), `?` (optional), `[abc]` / `[^abc]` (char class), `^` / `$` (anchors), `\d` / `\w` / `\s` (shorthand classes), `|` (alternation), `()` (grouping, no captures).
- Unsupported (defer): backreferences, lookahead/lookbehind, named groups, Unicode property escapes.

**Estimate**: 2-3 days.
**Risk**: Medium (NFA construction is well-understood; regex edge cases are numerous but bounded by the supported subset).
**Dependencies**: None for Phase 2. Phase 3 depends on SMT-string integration.

### 2. indexOf with computed fromIndex requires --object-bits 10

**What**: The solver-side path allocates many string objects, exceeding the default 256-object limit.

**Plan**: Two options:
1. **Auto-detect** (recommended): when the frontend emits solver-side string operations (ts_string_to_refined), count the allocations. If > 200, emit a warning at the end of type-checking suggesting `--object-bits 10`. ~1 hour.
2. **Reduce allocations**: reuse solver-side string buffers across calls in `ts_string_to_refined` by caching the refined view per source symbol. ~1-2 days, higher impact but more complex.

**Estimate**: Option 1: 1 hour. Option 2: 1-2 days.
**Risk**: Low.

### 3. Mixed-union-type arrays

**What**: `(number | number[])[]` crashes `simplify_member` in CBMC core.

**Plan**: Frontend workaround — detect the pattern (array element type is a union containing an array type) and emit a warning + nondet value instead of crashing. The core fix (teaching `simplify_expr_struct.cpp` to handle member access on union-typed array elements) is a separate CBMC-core task.

**Estimate**: Workaround: 1 hour. Core fix: 1-2 days (needs careful testing across all frontends).
**Risk**: Workaround: low. Core fix: medium.

### 4. Date calendar getters (getMonth, getDate, etc.)

**What**: Only getFullYear is implemented via epoch arithmetic; other calendar getters return nondet.

**Plan**: Implement the ES2024 §21.4.1 algorithms:
- `getMonth`: extract month from days-since-epoch (needs leap year table).
- `getDate`: day-of-month from days-since-epoch.
- `getDay`: `(days + 4) % 7` (Jan 1 1970 was Thursday = 4).
- `getHours/Minutes/Seconds/Milliseconds`: modular arithmetic on the time value.

The hours/minutes/seconds/milliseconds are simple: `Math.floor(time / 3600000) % 24`, etc. Month and day-of-month need the cumulative-days-per-month table with leap year handling.

**Estimate**: 1 day (hours/min/sec/ms are trivial; month/date need the table).
**Risk**: Low (pure arithmetic, well-specified).

### 5. Generator parameters (next(value))

**What**: `gen.next(42)` should send `42` as the result of the `yield` expression inside the generator. Currently ignored.

**Plan**: Extend the generator state machine:
- Each yield point becomes a "receive slot" in addition to a "send slot".
- `next(value)` stores `value` into the receive slot for the current state.
- The yield expression evaluates to the received value.
- Requires modelling the generator struct with both `__values` (outgoing) and `__inputs` (incoming) arrays.

**Estimate**: 1-2 days.
**Risk**: Medium (interaction with the state counter and pending_stmts ordering needs care).

### 6. yield* delegation

**What**: `yield* otherGenerator()` delegates to another generator, yielding all its values.

**Plan**: At conversion time, inline the delegated generator's yields into the parent's yield array. This is a syntactic transform: `yield* g()` becomes `yield g_val_0; yield g_val_1; ...` when the delegated generator has constant yields.

**Estimate**: Half day (for constant-yield delegates). Non-constant delegates would need runtime dispatch (much harder).
**Risk**: Low for constant case.

### 7. Symbol-keyed properties

**What**: `obj[Symbol.iterator]` and computed symbol keys can't be used as property accessors because our struct model uses named fields.

**Plan**: This is a fundamental model limitation. Two approaches:
1. **Well-known symbols only** (pragmatic): hardcode `Symbol.iterator`, `Symbol.toPrimitive`, etc. as special property names that the converter recognizes. ~1 day.
2. **Map-based property model** (full): replace struct fields with a Map-like key→value store. Multi-week refactor affecting every property access site. Not recommended.

**Estimate**: Pragmatic: 1 day. Full: 2+ weeks.
**Risk**: Pragmatic: low. Full: very high.

---

## Not modelled

### 8. Dynamic import()

**What**: `import("./module")` returns nondet (doesn't crash, but doesn't resolve the module).

**Plan**: Model as synchronous import — resolve the module path using the existing multi-file import infrastructure, load and convert it, return the module's exports object. The Promise wrapper is irrelevant (we model async as sequential).

**Estimate**: Half day.
**Risk**: Low (reuses existing module resolution).

### 9. WeakRef.deref()

**What**: `WeakRef` not modelled; `deref()` should return the referent or undefined.

**Plan**: Model `new WeakRef(target)` as storing `target`. `deref()` always returns the target (no GC in BMC). This is sound — if the program works with the referent always alive, it works in all executions.

**Estimate**: 1-2 hours.
**Risk**: None.

### 10. Proxy handler traps

**What**: `new Proxy(target, handler)` returns target unchanged; handler traps are ignored.

**Plan**: For full support, intercept every property access/method call on a Proxy-typed variable and dispatch through the handler's trap functions. This is a substantial refactor:
- Add a `__handler` field to the Proxy struct.
- On every `obj.prop` access, check if `obj` is a Proxy; if so, call `handler.get(target, "prop", receiver)`.
- Similarly for set, apply, construct, etc.

**Estimate**: Full: 5-7 days. Not recommended unless a specific user need arises.
**Risk**: High (touches every property access path).

### 11. Object.create / Object.setPrototypeOf

**What**: Static prototype chain only (resolved at conversion time from class declarations). Runtime prototype manipulation not supported.

**Plan**: 
- `Object.create(proto)`: create a new object whose type includes all of proto's fields. At conversion time, this is equivalent to `new` on a class that extends proto's type.
- `Object.setPrototypeOf(obj, proto)`: would require runtime prototype links (pointer to parent struct). This is the "Phase 2/3" from the original prototype-chain plan.

**Estimate**: Object.create: 1 day. setPrototypeOf: 2-3 days (needs runtime pointer-based chain walking).
**Risk**: Object.create: medium. setPrototypeOf: high.

---

## Priority ordering

Ordered by: (user-facing impact × feasibility) / risk.

| Priority | Item | Estimate | Impact | Risk |
|----------|------|----------|--------|------|
| **1** | RegExp Phase 2 (metacharacters) | 2-3 days | High (common pattern) | Medium |
| **2** | Date calendar getters (month, day, hours, etc.) | 1 day | Medium | Low |
| **3** | indexOf object-bits auto-detect warning | 1 hour | Low (UX) | Low |
| **4** | WeakRef.deref() | 1-2 hours | Low | None |
| **5** | Dynamic import() (resolve module) | Half day | Low (niche) | Low |
| **6** | Mixed-union arrays workaround | 1 hour | Low (rare) | Low |
| **7** | yield* delegation (constant case) | Half day | Low | Low |
| **8** | Generator next(value) parameters | 1-2 days | Medium | Medium |
| **9** | Symbol well-known symbols | 1 day | Low (niche) | Low |
| **10** | Object.create | 1 day | Low | Medium |
| **11** | Object.setPrototypeOf (runtime chain) | 2-3 days | Low | High |
| **12** | Proxy handler traps (full) | 5-7 days | Low | High |

### Recommended execution

**Quick wins (1 day total)**: Items 3, 4, 6 — three items under 2 hours each.

**High-impact feature**: Item 1 (RegExp Phase 2) — the single highest-impact remaining item. Opens verification of code using basic regex patterns.

**Medium features (1 week)**: Items 2, 5, 7, 8, 9 — each half-day to 2 days.

**Defer**: Items 10, 11, 12 — high risk, low user-facing impact. Address only when a specific user need arises.

### Infrastructure dependency

**SMT-string integration** (from tautschnig/py branch): enables RegExp Phase 3 (symbolic regex matching via `str.in_re`), improves symbolic string reasoning generally, and would make the indexOf object-bits issue disappear (SMT solvers handle strings natively without object-bit limits). This is the single most impactful infrastructure change but is a separate task from the frontend work listed above.
