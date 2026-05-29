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

### 1.6 Static prototype chain only

**What**: We resolve prototype chains at conversion time (parent
class fields are inlined into child structs; `isPrototypeOf` is
constant-folded). Runtime prototype manipulation (`Object.create`,
`Object.setPrototypeOf`, assignment to `obj.__proto__`) is not
modelled.

**Why**: Our struct model has no runtime prototype pointer. A full
runtime model would require pointer-based chain walking on every
property access (substantial overhead and complexity).

**Implication for security work**: We cannot directly verify
prototype-pollution propagation (e.g. that polluting
`Object.prototype.x = "bad"` later affects an unrelated object's
`.x`). Instead, the
`__CPROVER_assert_safe_property_key(k)` primitive lets harnesses
verify the *pre-condition*: "the attacker-controlled key must not
be `__proto__`, `constructor`, or `prototype`." This is the
contract that defensive code (e.g. lodash@4.18.0's `baseUnset`)
enforces; if it's broken at any sink, the harness fails.

**Tracking**: `regression/typescript/object-prototype-chain/`
(static chain works); `integration-lodash-cve-ghsa-f23m/`
(precondition-style detection works).

### 1.7 Null/undefined preservation in reference and array element types

**What**: For value-typed unions (`number | null`, `boolean | undefined`),
null and undefined are preserved as IEEE-754 quiet NaN sentinels
with distinct payloads (see §1.1) — the model is precise. For
reference-typed unions (`string | null`, `T[] | null`) the type
system collapses to the reference type without a sentinel for
the missing value; assignments like `let x: string | null = null`
silently coerce null to a garbage struct, and downstream null
checks return false.

**Where**: `src/typescript/typescript_converter.cpp` —
`convert_type` collapses `T | null` and `T | undefined` to
`T` for non-float `T`.

**Why**: A discriminated representation for every type would
either require a tagged-union runtime (significant overhead) or
a carefully chosen "null-zone" within each type's value space
(complex and type-specific). The current model trades precision
for simplicity.

**Implication for security work**:
`__CPROVER_assert_not_null(x)` is precise for value-typed `x` and
over-approximates to `true` for reference-typed `x` (sound for
"no crash" claims but imprecise — a real null could slip through
without being detected). Harnesses that need to verify null-safety
on reference-typed inputs should structure inputs as `number | null`
proxies or use object wrappers where the missing-ness is encoded
as a `_present: boolean` discriminator field.

**Tracking**: `regression/typescript/integration-qs-cve-ghsa-q8mj/`
(uses the value-typed proxy pattern).

---

## 2. Specification gaps (fixable; pending work)

Features where our implementation is incomplete relative to ES2024.
Each pending entry has a corresponding item in
[typescript-remaining-work-plan.md](typescript-remaining-work-plan.md)
with a planned approach and estimate. Resolved entries are kept inline
(as one-line redirects) so the section number remains stable; the
narrative for each fix lives in
[typescript-fixes-changelog.md](typescript-fixes-changelog.md).

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

### 2.9 `for..of` over method-call result with index-map writes inside (resolved 2026-05-29)

Resolved: see
[typescript-fixes-changelog.md](typescript-fixes-changelog.md) →
"Larger fixes (with narrative)" → `for-of-method-call-map-write`.
Regression guard: `regression/typescript/for-of-method-call-map-write/`
(CORE).

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

### 3.4 Taint analysis precision (resolved 2026-05-28)

Resolved: see
[typescript-fixes-changelog.md](typescript-fixes-changelog.md) →
"Larger fixes (with narrative)" →
`flow-no-source` / `flow-sanitized`. Regression guards under
`regression/typescript-taint/`.

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

### 4.3 Auto-generated harnesses from local pattern matches over-flag

**What**: When CBMC harnesses are auto-generated from a single CodeQL
pattern match (or any single-function syntactic match), the harness
treats the function's parameters as fully nondet. In real code, those
parameters are often constrained by validators applied 2–3 calls away
from the matched function. The auto-generated harness has no way to
express that without a cross-procedural data-flow source telling it
which `__CPROVER_assume` constraints to add.

**Where**: not a frontend bug per se — a property of how CBMC's
intra-procedural analysis composes with externally-driven harness
generators.

**Why**: CBMC analyses (symex, taint, model-checking) work on the
GOTO program of one entry function plus everything it calls. They do
not, by themselves, know which preconditions a *caller* of that
function would establish. CodeQL has cross-procedural data-flow
(`DataFlow::Global`) but our scale-triage pipeline currently uses
syntactic matches.

**Workaround**: The CodeQL→CBMC auto-triage pipeline
(`triage.py`) implements a manual cross-procedural pass: for each
CodeQL hit it traces the dangerous variable backwards to a function
parameter, greps callers, classifies their arguments as
literal / safe-source / unsafe-source, and emits matching
`__CPROVER_assume` constraints into the harness. This brought the
false-positive rate from ~40% to <2% on the AWS audit.

**Path to fix**: integrate the CodeQL `DataFlow::Global` API into the
triage queries themselves so the harness generator receives the
cross-procedural call-graph slice directly, rather than reconstructing
it via grep. Tracked as a follow-up in the security-tooling track.

---

## 5. Index of regression tests documenting limitations

Tests whose primary purpose is to document a limitation rather than
verify a feature. Use these as references when adding new ones.

| Test | Limitation |
|------|------------|
| `async-race-undetected` | Sequential async (KNOWNBUG by design) |
| `for-of-method-call-map-write` | Regression guard for §2.9 (resolved) — `member_exprt` invariant on `String.split` result iterated by `for..of` |
| `integration-tmp-cve-ghsa-7c78` | Multi-char string arrays through function calls (§1.3) |
| `integration-lodash-cve-ghsa-f23m` | Prototype-pollution detection via property-key contract (no runtime prototype-chain manipulation modelled) |
| `integration-qs-cve-ghsa-q8mj` | Null-deref detection via not-null contract on value-typed inputs (§1.7) |
| `integration-lodash-template-cve-r5fr` | Constant-string check is conversion-time only — applied at the application call-site, not inside the library |
| `integration-flatted-recursion-dos` | Recursion-DoS detection via input-size precondition + `--unwinding-assertions` |
| `integration-picomatch-cve-3v7f` | Method-injection detection via allowlist primitive |
| `optional-chaining` | Optional chaining on union method calls (§2 incomplete) |

---

## 6. Security primitives summary

The frontend provides several security-oriented assertion primitives.
Each is conversion-time (constant-fold when possible) with a
solver-side path for symbolic inputs. Use them as preconditions /
postconditions in security harnesses. All seven primitives below have
been exercised on real-world AWS TypeScript code (see §7) — both as
positive verification of defensive code and as detectors of buggy
patterns confirmed via end-to-end PoCs against published packages.

| Primitive | Catches | CVE pattern |
|-----------|---------|-------------|
| `__CPROVER_assert_no_path_traversal(s)` | `..` in path strings | tmp GHSA-7c78-jf6q-g5cm |
| `__CPROVER_assert_safe_property_key(k)` | `__proto__`, `constructor`, `prototype` | lodash GHSA-f23m-r3pf-42rh, flatted GHSA-rf6f-7fwh-wjgh |
| `__CPROVER_assert_not_null(x)` | null/undefined dereference | qs GHSA-q8mj-m7cp-5q26 |
| `__CPROVER_assert_constant_string(s)` | user-controlled string at code sink | lodash _.template GHSA-r5fr-rjxr-66jc |
| `__CPROVER_assert_no_template_metachars(s)` | `${`, `<%`, `<?`, `{{`, `<script` | template-injection class |
| `__CPROVER_assert_input_size_bounded(input, max)` | unbounded recursion DoS | flatted GHSA-q8gm-r3vv-cwfj |
| `__CPROVER_assert_in_allowlist(s, allowed)` | method/property injection via inherited keys | picomatch GHSA-3v7f-55p6-f55p |

Each has paired regression tests (`sec-*`) and integration tests
(`integration-*-cve-*` for the vulnerable pattern, `integration-*-fixed-*`
for the defensive pattern) under `regression/typescript/`.

## 7. Scale validation evidence

The frontend has been exercised at scale on real-world TypeScript code
through a CodeQL→CBMC auto-triage pipeline run against AWS-related
repositories. This section records what that exercise tells us about
the maturity and limits of the frontend.

### Run shape

- **Repos audited**: 35 (AWS SDKs, CDK constructs, smithy, amplify-*,
  strands-agents, cbmc-proof-debugger, etc.).
- **CodeQL queries**: 7 anti-pattern queries spanning the full primitive
  taxonomy (path traversal, prototype pollution via `in` / `for..in` /
  `Object.assign(JSON.parse)` / map-or-empty / `path.normalize` includes,
  unbounded recursion).
- **Hits processed**: 301.
- **CBMC harnesses synthesised**: one per hit; each harness is the
  outermost named enclosing function, with cross-procedural
  `__CPROVER_assume` constraints derived from caller analysis (see §4.3).

### What the run tells us about frontend robustness

- **Frontend invariant violations**: one class observed across all 301
  harnesses (the `member_exprt`-on-`String.split`-result violation, now
  §2.9 — resolved). This is meaningful: the frontend now consumes
  arbitrary AWS-quality TypeScript code without crashing.
- **Triage outcomes**: 296 TRUE_POSITIVE, 5 FALSE_POSITIVE on the final
  pass after §2.9 was fixed and after triage.py grew its cross-procedural
  pass (see §4.3). The remaining FALSE_POSITIVEs are validators applied
  via callers the local pattern match did not see.
- **Distinct security findings**: 14, of which 6 are High severity. Each
  has an end-to-end PoC running against the published package or the
  actual source. The findings, PoCs, and disclosure correspondence live
  outside this repository (kept private until coordinated disclosure
  completes).

### What this validates about the security primitives

All seven primitives in §6 were exercised in synthesised harnesses
during the run. None hit a soundness regression. A handful of
imprecisions surfaced (constant-folding fallback for non-literal
allowlists, prototype-chain `in` vs. `Object.hasOwn`); these are
documented in [over-approximation-audit.md](over-approximation-audit.md).

### What this does NOT validate

- The audit ran the frontend up to GOTO conversion + intra-procedural
  symbolic execution. It did **not** stress-test the symbolic-string
  solver at scale — most harnesses had string content small enough to
  constant-fold. §1.2, §1.3, and §3.1 remain the relevant string-model
  scaling limits.
- The audit did not exercise async / Promise.all paths; §1.5 is
  unchanged by this evidence.
- The audit was driven by syntactic CodeQL matches; it did not exercise
  the type-checking front-half of the frontend on novel programs.

### Where the scale-validation infrastructure lives

The CodeQL pack (`aws-ts-anti-patterns/`) and the CBMC auto-triage
pipeline (`triage.py`) live outside this repository for now (the audit
surfaced findings under coordinated disclosure). Once disclosure
completes, the intent is to publish the pack as a standalone tool that
any TypeScript-using project can run for its own audit; that publication
will be cross-referenced from this section.

## 8. Cross-references

- [typescript-remaining-work-plan.md](typescript-remaining-work-plan.md) — prioritized future work
- [typescript-capability-matrix.md](typescript-capability-matrix.md) — per-feature support status
- [typescript-fixes-changelog.md](typescript-fixes-changelog.md) — history of bugs fixed
- [over-approximation-audit.md](over-approximation-audit.md) — sound over-approximations
- [typescript-status-2026-05-16.md](typescript-status-2026-05-16.md) — current state snapshot
- [refined-string-migration-plan.md](refined-string-migration-plan.md) — long-term string model migration
- [knownbug-resolution-plans.md](knownbug-resolution-plans.md) — historical KNOWNBUG plans
