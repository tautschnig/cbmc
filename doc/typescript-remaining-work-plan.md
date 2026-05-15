# TypeScript Frontend — Remaining Work Plan

## Current state (2026-05-15)

- 692 CORE tests, 2 KNOWNBUGs (async-race-undetected, object-prototype-chain)
- strict-nan-not-equal: CLOSED (distinct NaN payloads)
- 4 real-world harnesses passing (semver, URL, email, config)

---

## 1. object-prototype-chain (KNOWNBUG)

**What's broken**: `Object.getPrototypeOf(x)`, `x instanceof Y` via prototype
chain, method resolution through `__proto__` links. Our struct model has no
prototype chain — each class is a flat struct with its own methods inlined.

**Plan**:

Phase 1 — Prototype link field (~2 days):
- Add a `__proto__` field (pointer to parent struct type) to every class struct.
- `Object.getPrototypeOf(x)` returns `x.__proto__`.
- `x instanceof Y` checks if Y.prototype appears anywhere in x's chain.
- Limit chain depth to 5 (configurable) to bound verification.

Phase 2 — Method resolution through chain (~1-2 days):
- When a method call `x.foo()` fails to find `foo` in x's own struct,
  walk `x.__proto__` looking for the method.
- This enables inherited methods that aren't explicitly copied into subclass
  structs (currently we copy them during class conversion, which works for
  direct inheritance but not for runtime prototype manipulation).

Phase 3 — `Object.create`, `Object.setPrototypeOf` (~1 day):
- `Object.create(proto)` creates a new object with `__proto__ = proto`.
- `Object.setPrototypeOf(obj, proto)` mutates the link.

**Estimate**: 4-5 days total.
**Risk**: Medium-high. Prototype chains interact with every method call site;
the fallback-to-parent lookup adds complexity to the call dispatcher.
**Dependencies**: None.

---

## 2. Documented gaps

### 2.1 for-of on strings

**What's broken**: `for (const c of "abc")` is a no-op with a warning.

**Plan**: Rewrite at the frontend level to:
```
for (let __i = 0; __i < s.length; __i++) {
    const c = s.charAt(__i);  // 1-char string
    <body>
}
```
This is a syntactic transform — no solver implications.

**Estimate**: 2-3 hours.
**Risk**: Low.
**Dependencies**: None (charAt on non-constant receivers already works after A2).

### 2.2 Ternary with literal-union inference

**What's broken**: `0 ? 10 : 20` has TS-inferred type `10 | 20` (a literal
union). Our tagged-union representation can't reliably match the target integer.

**Plan**: When the ternary's inferred type is a numeric literal union AND both
branches are constant numbers, bypass the union and emit a plain `if_exprt`
with `double_type()` result. Detect via the `_type` field containing `|` with
all-numeric alternatives.

**Estimate**: 1-2 hours.
**Risk**: Low.
**Dependencies**: None.

### 2.3 Mixed-union-type arrays

**What's broken**: `(number | number[])[]` crashes `simplify_member`.

**Plan**: This is a CBMC-core simplifier issue — it doesn't handle member
access on a union-typed array element. Two options:
1. Frontend workaround: detect the pattern and emit a nondet value with a
   warning (like for-of on strings). Quick but unsatisfying.
2. Core fix: teach `simplify_expr_struct.cpp` to handle the case. Requires
   understanding the simplifier's invariant at line 122.

**Estimate**: Option 1: 1 hour. Option 2: 1-2 days (core change, needs
careful testing across all frontends).
**Risk**: Option 1 low, option 2 medium.
**Dependencies**: None.

### 2.4 RegExp metacharacters (Phase 2)

**What's broken**: `/a.b/.test(s)` treats `.` as a literal dot, not "any char".

**Plan**:
- Build a small NFA from the regex pattern at conversion time.
- For constant input strings: simulate the NFA and return true/false.
- For symbolic input: return nondet (Phase 3 with SMT-string theory would
  handle this via `str.in_re`).
- Supported metacharacters: `.`, `*`, `+`, `?`, `[abc]`, `[^abc]`, `^`, `$`,
  `\d`, `\w`, `\s`, `|`, `()` (grouping only, no captures).

**Estimate**: 2-3 days.
**Risk**: Medium (NFA construction is well-understood but regex edge cases
are numerous).
**Dependencies**: None for Phase 2. Phase 3 depends on SMT-string integration.

### 2.5 indexOf with computed fromIndex requires --object-bits 10

**What's broken**: The solver-side path allocates many string objects, exceeding
the default 256-object limit.

**Plan**: This is a scalability issue, not a correctness issue. Options:
1. Auto-detect when string-heavy code needs more object bits and emit a
   warning suggesting `--object-bits 10`.
2. Reduce allocations by reusing solver-side string buffers across calls
   (requires changes to `ts_string_to_refined`).
3. Accept as a documented limitation (users pass `--object-bits 10`).

**Estimate**: Option 1: 1 hour. Option 2: 1-2 days. Option 3: 0.
**Risk**: Low.
**Dependencies**: None.

### 2.6 Set<string>

**What's broken**: `new Set<string>()` uses the numeric Set type (double[8]).

**Plan**: Parameterize the Set type infrastructure:
- Register `Set<string>` with `typescript_string[8]` data at startup.
- Register a separate `Set<string>::__init__` constructor.
- The `add`/`has`/`delete` methods already use `data_arr_type.element_type()`
  for comparison — they just need the correct type to be selected.
- Key issue from earlier attempt: the constructor is shared. Fix: dispatch
  constructor by the variable's declared type, not by `cls_name`.

**Estimate**: Half day.
**Risk**: Low (diagnosis is clear from the earlier attempt).
**Dependencies**: None.

### 2.7 Calendar getters on Date

**What's broken**: `getFullYear()`, `getMonth()`, etc. return nondet.

**Plan**: Implement epoch-to-calendar conversion:
- `getFullYear`: `Math.floor(time / 31557600000) + 1970` (approximate; real
  algorithm needs leap year handling).
- Full implementation: port the ES2024 §21.4.1.3 MakeDay / MakeDate / TimeClip
  algorithms. These are pure arithmetic on the timestamp.
- Alternatively: implement only `getFullYear` precisely (most commonly
  verified) and leave others as nondet.

**Estimate**: 1 day for getFullYear only; 2-3 days for full calendar.
**Risk**: Low (pure arithmetic, well-specified).
**Dependencies**: None.

### 2.8 >>> on BigInt

**What's broken**: `>>>` on BigInt is treated as signed shift; spec says TypeError.

**Plan**: Emit a verification-error assertion (`ASSERT false` with property
class "type-error") when `>>>` is applied to bigint operands. This matches
the spec's runtime TypeError semantics.

**Estimate**: 30 minutes.
**Risk**: None.
**Dependencies**: None.

---

## 3. Not modelled

### 3.1 Generators (function*)

**What's broken**: Generator functions and `yield` are not parsed or converted.

**Plan**:
- Parse `FunctionDeclaration` with `asteriskToken` and `YieldExpression`.
- Model as a state machine: each `yield` point becomes a state; `next()`
  advances to the next state and returns `{ value, done }`.
- For bounded verification: unroll the generator up to `--unwind` iterations.
- The `for-of` on a generator would use the same indexed-iteration rewrite.

**Estimate**: 3-4 days.
**Risk**: Medium (state-machine encoding is non-trivial; interaction with
closures needs care).
**Dependencies**: None.

### 3.2 Dynamic import()

**What's broken**: `import("./module")` is not handled.

**Plan**: Model as a synchronous import (same as static `import`). The
dynamic nature (Promise-returning) is irrelevant for verification — we
already model async as sequential. The module resolution path already
handles `./relative` imports.

**Estimate**: Half day.
**Risk**: Low.
**Dependencies**: None.

### 3.3 WeakMap / WeakRef

**What's broken**: Not modelled at all.

**Plan**: Model WeakMap identically to Map (our model doesn't have GC, so
"weak" has no semantic difference for verification). WeakRef.deref() returns
the referent or undefined (model as: always returns the referent, since GC
doesn't run during bounded verification).

**Estimate**: Half day (reuse Map infrastructure).
**Risk**: Low.
**Dependencies**: None.

### 3.4 Proxy / Reflect

**What's broken**: Not modelled.

**Plan**: This is the hardest item. Proxy intercepts ALL property access,
method calls, and operators on an object. Full support would require:
- A dispatch layer that checks for a Proxy wrapper before every property
  access and method call.
- Handler trap functions (get, set, apply, construct, etc.).

Pragmatic approach: model `new Proxy(target, handler)` as returning `target`
unchanged (ignoring the handler). This is sound for programs that don't
rely on trap side effects, which covers most verification use cases.

**Estimate**: Pragmatic: 1 hour. Full: 5-7 days.
**Risk**: Pragmatic: low. Full: high.
**Dependencies**: object-prototype-chain (for full Reflect.getPrototypeOf).

### 3.5 Symbol

**What's broken**: `Symbol()`, `Symbol.iterator`, well-known symbols.

**Plan**:
- Model `Symbol()` as a unique integer (fresh nondet, constrained to be
  different from all other symbols via assume).
- `Symbol.iterator` and other well-known symbols: fixed constants.
- Property access via computed symbol keys: not supported (would need a
  map-based property model instead of struct fields).

**Estimate**: 1-2 days for basic Symbol(); well-known symbols add 1 day.
**Risk**: Medium (symbol-keyed properties are a fundamental model change).
**Dependencies**: None for basic; prototype chain for Symbol.iterator usage.

---

## Priority ordering

Ordered by: (user-facing impact × feasibility) / risk.

| Priority | Item | Estimate | Impact | Risk |
|----------|------|----------|--------|------|
| **1** | 2.1 for-of on strings | 2-3 hours | High (common pattern) | Low |
| **2** | 2.2 Ternary literal-union | 1-2 hours | Medium (common annoyance) | Low |
| **3** | 2.8 >>> on BigInt TypeError | 30 min | Low (correctness) | None |
| **4** | 2.6 Set<string> | Half day | Medium (closes matrix gap) | Low |
| **5** | 2.4 RegExp Phase 2 (metacharacters) | 2-3 days | High (common pattern) | Medium |
| **6** | 2.7 Calendar getters (getFullYear) | 1 day | Medium | Low |
| **7** | 3.2 Dynamic import() | Half day | Low (niche) | Low |
| **8** | 3.3 WeakMap/WeakRef | Half day | Low (niche) | Low |
| **9** | 2.3 Mixed-union arrays (workaround) | 1 hour | Low (rare pattern) | Low |
| **10** | 1. object-prototype-chain | 4-5 days | Medium (closes KNOWNBUG) | Medium-high |
| **11** | 3.1 Generators | 3-4 days | Medium (growing usage) | Medium |
| **12** | 3.5 Symbol (basic) | 1-2 days | Low (niche) | Medium |
| **13** | 2.5 object-bits auto-detect | 1 hour | Low (UX) | Low |
| **14** | 3.4 Proxy/Reflect (pragmatic) | 1 hour | Low (niche) | Low |
| **15** | 3.4 Proxy/Reflect (full) | 5-7 days | Low | High |

### Recommended execution tracks

**Track A — Quick wins (1-2 days total)**:
Items 1, 2, 3, 9, 13, 14. All under 2 hours each. Clears 6 documented gaps.

**Track B — Medium features (1 week)**:
Items 4, 5, 6, 7, 8. Each is half-day to 3 days. Adds Set<string>, RegExp
metacharacters, Date calendar, dynamic import, WeakMap.

**Track C — Substantial (2 weeks)**:
Items 10, 11, 12. Prototype chain, generators, Symbol. These are the
remaining "hard" items that require model-level changes.

**Suggested order**: Track A first (quick wins), then Track B (medium
features), then Track C (substantial) — unless a specific user need
prioritizes something from Track C.
