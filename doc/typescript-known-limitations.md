# TypeScript Frontend — Known Limitations and Loose Ends

This is the authoritative tracker for limitations, architectural debt,
and external dependencies in the CBMC TypeScript frontend. It is the
entry point for understanding what does not yet work, what is blocked
on external infrastructure, and what we have deliberately deferred.

For planned future work in priority order, see
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md).

For per-feature support status, see
[typescript-capability-matrix.md](typescript-capability-matrix.md).

For the history of fixes (with commits and regression tests), see
[typescript-fixes-changelog.md](typescript-fixes-changelog.md).

For the catalog of sound over-approximations, see
[over-approximation-audit.md](over-approximation-audit.md).

---

## How to use this document

When you encounter a limitation while working on the frontend:

1. Check the relevant section below.
2. If the limitation is novel, add an entry. Each entry should record:
   - **What**: a one-line description of the limitation.
   - **Where**: the affected feature(s), module(s), or test(s).
   - **Why**: the root cause (briefly).
   - **Workaround**: what users can do today.
   - **Tracking**: link to a regression test that exercises (or
     documents) the limitation, or `none`.
3. If the limitation has a path to a fix, add it to
   [typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
   with a priority and estimate. Cross-reference it from here.

Entries that have been fully resolved should be moved to
[typescript-fixes-changelog.md](typescript-fixes-changelog.md).

---

## 1. Architectural / model limitations

These are limitations of the model itself — choices that were made for
tractability and that constrain what we can verify.

### 1.1 NaN sentinels for null/undefined

**What**: `null` and `undefined` are modelled as IEEE-754 quiet NaNs
with distinct payloads (1 and 2 respectively); real NaN has payload 0.

**Why**: Allows uniform numeric type for any/optional fields without
a tagged-union runtime cost. Distinct payloads enable correct
`null === null`, `null !== undefined`, `Number.isNaN(NaN)` etc.

**Trade-offs**:
- Bitwise inspection of NaN bits would observe non-canonical NaNs.
- Programs that intentionally use NaN payloads for IPC (very rare)
  could be confused.

**Tracking**: `regression/typescript/strict-nan-not-equal/`,
`regression/typescript/null-safety/`.

### 1.2 Inline-array string model

**What**: TypeScript strings are modelled as
`struct { length: signedbv[32], data: unsignedbv[16][64] }` — a
fixed 64-code-unit buffer.

**Why**: Compiles cleanly to bit-vectors for the SAT backend; lets
operations on small constant strings constant-fold at conversion time.

**Trade-offs**:
- Strings longer than 64 code units are truncated.
- The 1056-bit-per-string footprint compounds when strings appear in
  arrays (see §1.3 below).
- `--object-bits 10` is sometimes required when many solver-side
  refined-string allocations are needed (the frontend now warns
  when this is likely; see commit `5c06761f7e`).

**Tracking**: `regression/typescript/spec3-string-utf8-bmp/`,
`regression/typescript/spec3-string-utf16-astral/`. Migration to a
solver-side string-only model is tracked in
[refined-string-migration-plan.md](refined-string-migration-plan.md).

### 1.3 String-array literal explosion through function-call boundaries

**What**: Passing `string[]` (where strings have multi-character
content) through a function call hits CBMC's `boolbv_map` literal-count
limit, manifesting as either an invariant violation
(`variable number of non-constant literals shall be within bounds`)
or `SAT checker ran out of memory`.

**Where**: `src/typescript/typescript_converter_*.cpp` — the model
itself; affects any harness that calls `f(arr)` where `arr: string[]`
with multi-char strings.

**Why**: A `string[]` is internally
`struct { length: int32, data: string[64] }` where each `string` is
1056 bits. With max 64 elements, an array is up to ~67 KB of bool
literals. Passing through a function call duplicates the bit-vector,
and downstream operations (e.g. `arr.join("")`) compound the literal
count past CBMC's default limit.

**Workaround**: Keep array operations inline at the call site (don't
pass multi-char-element arrays as function parameters). The
`integration-tmp-cve-ghsa-7c78` test illustrates this pattern.

**Tracking**: `regression/typescript/integration-tmp-cve-ghsa-7c78/`
(documents the workaround in comments).

**Path to fix**: would require either (a) shrinking the per-string
buffer (constraining all strings ≤ 16 code units?), or (b) migrating
to refined-string-only representation. Tracked in
[refined-string-migration-plan.md](refined-string-migration-plan.md).

### 1.4 Symbolic string array operations limited to constant fold

**What**: `arr.join(sep)`, `arr.sort(cmp)`, etc. on `string[]` only
constant-fold when the array elements are constants known at
conversion time. When an array passes through a function parameter or
contains a `nondet_string()` element, these methods either fall back
to `nondet_string()` or produce a string of a single character per
element.

**Where**: `src/typescript/typescript_converter_call.cpp` — the
`method == "join"` and similar handlers.

**Why**: Constant-folding is the simplest tractable model. A
solver-side join would require a refined-string formulation that
encodes the iterative concatenation, which the refined-string solver
does not currently expose at the level of granularity needed.

**Workaround**: For symbolic content, work with a single string and
indexed slicing rather than array operations.

**Tracking**: implicit in the integration-tmp tests' inline structure.

### 1.5 Sequential async / promises by default

**What**: `async`/`await` execute sequentially in source order; data
races between concurrent promises are not detected.

**Why**: Modelling concurrency with all interleavings is exponentially
expensive in BMC. Most JavaScript/TypeScript code is single-threaded
by design; sequential-async is sound for that majority.

**Workaround**: The `--ts-async-threading` flag enables a
race-detection mode (opt-in).

**Tracking**: `regression/typescript/async-race-undetected/`
(KNOWNBUG by design).

---

## 2. Specification gaps (fixable; pending work)

Features where our implementation is incomplete relative to ES2024.
Each has an entry in
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
with a planned approach and estimate.

### 2.1 RegExp metacharacters (Phase 2)

**What**: `/a.b/.test(s)` treats `.` as literal; `*`, `+`, `?`, `[]`,
`^`, `$`, `\d`, `\w`, `\s`, `|` not yet supported.

**Workaround**: Use literal regex patterns or rewrite tests as
explicit string predicates.

**Tracking**: see
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
item 1. Blocked on: nothing (Phase 2 is a self-contained NFA addition).

**External dependency for Phase 3**: SMT-string integration from the
`tautschnig/py` branch — would enable symbolic regex matching via
`str.in_re`. Currently waiting for upstream debugging to settle.

### 2.2 Date calendar getters

**What**: `getMonth`, `getDate`, `getDay`, `getHours`, etc. return
nondet (only `getFullYear` is implemented via epoch arithmetic).

**Tracking**: see
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
item 2.

### 2.3 Generator next(value) parameters

**What**: `gen.next(42)` ignores the argument; the `yield` expression
inside the generator does not receive the sent value.

**Tracking**: see
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
item 8.

### 2.4 yield* delegation

**What**: `yield* otherGen()` not supported.

**Tracking**: see
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
item 7.

### 2.5 Symbol-keyed properties

**What**: `obj[Symbol.iterator]` and other symbol keys can't be used
because struct fields use string names.

**Tracking**: see
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
item 9.

### 2.6 Object.create / Object.setPrototypeOf

**What**: Static prototype chain (resolved at conversion from class
declarations) only; runtime prototype manipulation not supported.

**Tracking**: see
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
items 10, 11.

### 2.7 Proxy handler traps

**What**: `new Proxy(target, handler)` returns `target` unchanged;
handler traps (`get`, `set`, `apply`, etc.) are ignored.

**Workaround**: For programs whose correctness does not depend on
the Proxy's trap behavior, the pragmatic model is sound.

**Tracking**: see
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
item 12.

### 2.8 Mixed-union-type arrays

**What**: `(number | number[])[]` previously crashed
`simplify_member`. Workaround in place: detected at conversion time,
emits warning and uses nondet (sound but imprecise).

**Tracking**: see
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
item 6 (workaround landed in commit `5c06761f7e`; full core fix
deferred).

---

## 3. Solver-side limitations

Limitations rooted in the SAT/SMT backend or refined-string solver
that affect what we can express, regardless of frontend completeness.

### 3.1 boolbv_map literal-count limit

**What**: CBMC's bit-vector encoder hits an internal invariant when
the number of non-constant literals exceeds the prop allocator's
range. Manifests for large symbolic structs (especially string
arrays — see §1.3) and for certain refined-string compositions.

**Where**: `src/solvers/flattening/boolbv_map.cpp:91`.

**Workaround**: `--object-bits 10` for the "too many addressed
objects" variant; otherwise, simplify the harness to reduce the
literal count.

**Tracking**: triggered by `regression/typescript/sec-tmp-cve-array-bypass/`
(SAT-checker complaints visible in output but verification still
correct).

### 3.2 SMT-string integration pending

**What**: The frontend uses CBMC's refined-string solver
(SAT-based) for symbolic string operations. SMT-string support
(`str.in_re`, `str.contains` on symbolic regex, etc.) would enable
RegExp Phase 3 and improve symbolic string reasoning generally.

**External dependency**: the `tautschnig/py` branch is debugging
SMT-string integration. We are waiting for that to settle.

**Tracking**: see [refined-string-migration-plan.md](refined-string-migration-plan.md).

### 3.3 Independently-valuable solver-side fixes not yet upstreamed

**What**: Four fixes in `src/solvers/strings/` were made during this
work that are independently valuable to CBMC core. They have not yet
been pushed upstream (deliberately; we accumulate on this branch).

**Tracking**: commits in `src/solvers/strings/` since the start of
this branch. To be filed separately as upstream PRs when this branch
stabilises.

---

## 4. UX / ergonomics

Items that are not soundness or precision issues but affect usability.

### 4.1 Error messages from the AST server

**What**: When `ts_ast_server.js` rejects a TypeScript program (e.g.
syntax error), the error surfaced to the user is sometimes terse.

**Tracking**: none currently; raise a regression test if this comes
up again.

### 4.2 No type-checked harness templates

**What**: Users writing security/property harnesses must manually
combine `nondet_*()` primitives. A library of common harness
templates (path-traversal, prototype-pollution, crash-on-null) would
improve the onboarding experience.

**Tracking**: planned as part of the security track (path-traversal
primitive landed in commit for `__CPROVER_assert_no_path_traversal`;
prototype-pollution and crash-on-null primitives are next phases).

---

## 5. Index of regression tests documenting limitations

Tests whose primary purpose is to document a limitation rather than
verify a feature. Use these as references when adding new ones.

| Test | Limitation |
|------|------------|
| `async-race-undetected` | Sequential async (KNOWNBUG by design) |
| `integration-tmp-cve-ghsa-7c78` | Multi-char string arrays through function calls (§1.3) |
| `optional-chaining` | Optional chaining on union method calls (§2 incomplete) |

---

## 6. Cross-references

- [typescript-remaining-work-plan.md](typescript-remaining-work-plan.md) — prioritized future work
- [typescript-capability-matrix.md](typescript-capability-matrix.md) — per-feature support status
- [typescript-fixes-changelog.md](typescript-fixes-changelog.md) — history of bugs fixed
- [over-approximation-audit.md](over-approximation-audit.md) — sound over-approximations
- [typescript-status-2026-05-16.md](typescript-status-2026-05-16.md) — current state snapshot
- [refined-string-migration-plan.md](refined-string-migration-plan.md) — long-term string model migration
- [knownbug-resolution-plans.md](knownbug-resolution-plans.md) — historical KNOWNBUG plans
