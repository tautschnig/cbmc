# Python Frontend — Blocked-Items Unblocking Plan

This document captures detailed, phased plans for the architectural items
that have come up during wave-41 work and are currently blocked. Items are
prioritised by impact × tractability × correctness. Created
2026-05-29 at PASS 2851 cumulative wave 41.

## Priority order

| Rank | Item | Tests gained (est.) | Effort | Risk |
|---|---|---:|---|---|
| P0 | None-encoding refactor (subsumes typed-numeric `is None` and `wrap_value` None-tagging) | ~5–15 | Very high | High blast radius, but unlocks broad correctness |
| P1 | Symbolic string subscript via `cprover_string_substring` | ~5–10 | High | String-solver behaviour risks |
| P2 | Symbolic-string genexp iteration (depends on P1) | ~3–5 | Medium-high | Refined-string SAT-loop |
| P3 | Bare `list` → `list[Any]` | ~1–3 | Medium | match-sequence-mapping regression |

The reasoning behind the ordering: P0 fixes a deep PLR-correctness defect
that we currently work around with brittle special-cases. P1 unlocks an
entire failing test cluster cheaply once we understand the
refined-string-solver's substring intrinsic. P2 builds on P1 and the
byte-OR fast-path landed in cluster v9. P3 is tractable but small impact.

---

## P0 — None-encoding refactor (subsumes typed-`is None` + `wrap_value` None-tagging)

### Root cause

Today we encode `None` as the integer sentinel `-2^62` in
`signedbv_typet{64}`. This is structurally indistinguishable from a
legitimate int with that value. Two PLR violations follow:

1. `nondet_int()` can produce `-2^62`, so `y is None` for
   `y: int = nondet_int()` resolves to True with non-zero probability —
   but in PLR an int is *never* None.
2. `wrap_value(int_constant)` blindly tags as `INT`, so `return None`
   from a function whose return type infers to `python_value` produces
   `{INT, sentinel, ...}` instead of `{NONE, ...}`. The
   "incidental pass" of `union-return-type` relied on this wrong
   encoding (sentinel-value match in the int field).

The current workaround (typed-numeric `is None` ⇒ False fast-path)
breaks `limit-none-identity`'s `x = None; assert x is None`, because
`x` ends up as `signedbv` carrying the sentinel value.

### Plan

**Phase 0.A — Audit sentinel sites** (read-only):
1. `grep -n "none_sentinel\|-4611686018427387904\|-2^62" src/python/` —
   enumerate every producer and consumer.
2. Tag each site as either:
   - **Producer**: writes the sentinel as a value
     (e.g. `convert_expression(None)`, default arg, `return` of None,
     etc.).
   - **Consumer**: tests an exprt against the sentinel
     (Is/IsNot, optional_params fast-path, truthy unwrap, etc.).
3. Build a checklist; expect ~15–25 sites.

**Phase 0.B — Introduce `python_none_value()` helper**:
- New helper in `python_value_type.h`:
  ```cpp
  inline struct_exprt python_none_value()
  {
    return make_python_value(
      python_type_tagt::NONE, from_integer(0, signedbv_typet{64}));
  }
  ```
- Returns the canonical NONE-tagged python_value.

**Phase 0.C — Migrate producers**:
- `convert_expression(None_constant)` returns `python_none_value()`
  (was: int sentinel).
- `convert_return` for bare `return` statement: `python_none_value()`.
- `wrap_value`: when input is the int sentinel constant, return
  `python_none_value()`. When input is any other typed value, keep the
  current path (no NONE wrapping for ordinary ints).
- Default arg binding (already partially done in cluster v9): emit
  `python_none_value()` instead of int sentinel for `param: T = None`
  where T isn't a numeric type. For numeric T (Optional[int]
  etc.) emit `python_none_value()` and let Optional-aware Is/IsNot
  handle it.

**Phase 0.D — Migrate consumers**:
- `is_python_value_type(left) and right == sentinel` Is path: keep
  `python_value_is(NONE)`.
- Add a new path for
  `is_python_value_type(left) and is_python_value_type(right) and
  right is python_none_value`: same `python_value_is(NONE)`.
- Typed-numeric `Is None`: now safe to return `false_exprt{}`
  unconditionally — there's no sentinel-value collision.
- Typed-numeric `IsNot None`: `true_exprt{}`.
- `optional_params` machinery: a `Optional[int]` param bound to None
  now arrives as `python_value{NONE,...}`; the type-mismatch loop
  needs to convert it to either the typed slot (if needed for
  arithmetic inside the callee) or keep it tagged. Likely a small
  adapter: when `optional_params.count(p) > 0 and arg.type ==
  python_value_type`, store as python_value instead of safe_typecasting
  back to int. This means optional-param storage type becomes
  python_value, not the underlying typed slot.

**Phase 0.E — Test fixups**:
- `limit-none-identity` should keep passing (`x = None` now
  python_value{NONE}; `is None` → True via `python_value_is`).
- `union-return-type` should keep passing without the workaround.
- Test the heterogeneous-default cluster (Optional[str], Optional[int])
  — likely several gains.

**Phase 0.F — Clean up old workarounds**:
- Remove the `is_optional_sym` skip in struct-vs-sentinel False
  fast-path (no longer needed).
- Remove the cluster-v9 length-zero Optional[str] fast-path (if
  migration handles it cleanly via tag).

### Expected gains
- `nondet_list4` (typed-int never None)
- Several `optional*`-class tests
- Possibly `github_3733`-class indirectly (if class field reads return
  python_value with proper None tag)
- Cleaner code, less special-casing

### Risks
- ~15–25 sites; each needs verification against regressions.
- Optional-param storage type change may cascade.
- Suggested approach: dedicated branch, run regression sweep after
  each phase, revert phases that lose ground.

---

## P1 — Symbolic string subscript via `cprover_string_substring`

### Root cause

`s[i]` for symbolic `s` currently builds
`{1, address_of(local_arr[0])}` where
`local_arr[0] = *(s.data + i)`. This captures the byte at conversion
time, but the refined-string solver treats the result as a fresh
string disconnected from `s`. So `assume(s == "abc")` does not
propagate to `s[0] == "a"`.

We tried a "view" `{1, s.data + i}` but it dangles when `s[i]`
escapes the function (regression in `github_3916` / `last(a)`).

### Plan

**Phase 1.A — Investigate `cprover_string_substring_func` semantics**:
1. Read `src/solvers/strings/string_constraint_generator_main.cpp` for
   the substring intrinsic's contract: does
   `cprover_string_substring(s, i, i+1)` register the result as a
   substring of `s` such that the solver propagates byte-level
   constraints from `s == "abc"`?
2. Test in a minimal CBMC unit test: assume two refined strings are
   content-equal; substring of one; assert substring's bytes match.
3. If the intrinsic propagates correctly, proceed to Phase 1.B.
   Otherwise, escalate or fall back to Phase 1.C.

**Phase 1.B — Replace `s[i]` with substring**:
- For symbolic `s` and index `i`:
  ```cpp
  // c = cprover_string_substring(s, i, i+1)
  exprt result = emit_string_function(
    ID_cprover_string_substring_func,
    {i_as_int, plus_exprt{i_as_int, from_integer(1, ...)}},
    symbol_table, pending_checks);
  // result is a fresh refined-string symbol of length 1
  ```
- For constant `s`, keep the conversion-time UTF-8 walk (already
  correct).
- For escape: the substring intrinsic produces a fresh refined-string
  symbol with its own backing storage. No dangling.

**Phase 1.C — Fallback if substring doesn't propagate**:
- Track a per-symbol "view of source" map: when `c = s[i]` is assigned,
  record `c → (s, i, 1)` (source, offset, length).
- At Eq/comparison sites, when both sides have known views, compare
  via byte-level reads of the sources directly.
- This is what the cluster-v9 1-char-vs-1-char-const fast-path tried,
  but extending it to view-tracked symbols requires the new map.

**Phase 1.D — Extend to slicing**:
- `s[a:b]` becomes `cprover_string_substring(s, a, b)`.
- Closes additional tests in the slicing cluster
  (`string-symbolic-7`, etc.).

### Expected gains
- `string-char-symbolic-success`, `string-nondet-index-success`,
  `string-symbolic-7`
- `string-replace-count-nondet-success`, `string-rfind-nondet`,
  `string-rstrip-nondet`
- Possibly some `crash-unicode-source`-adjacent tests

### Risks
- Refined-string solver is the same code path that caused the
  SAT-loop crash on `github_3036_6`. If
  `cprover_string_substring_func` triggers similar pathological loops,
  we'd need to gate it (e.g., only for short bounded `i`).
- Slicing semantics around negative indices, step != 1, etc. need
  separate handling.

---

## P2 — Symbolic-string genexp iteration

### Root cause

`all(c in valid_chars for c in s)` for symbolic `s` falls to the
"unsupported iterable" fallback. We tried unrolling the genexp 8-way
over `s.data[i]`, but the body's `c in valid_chars` invokes
`cprover_string_contains_func` 8 times, which causes the
refined-string solver loop to crash with `vector::reserve` invariant
violation.

The cluster-v9 fast-path (1-char-in-const-string OR over bytes) is
in place to bypass `cprover_string_contains_func`, but only fires
when the LHS is a struct literal — not when bound through a loop
variable symbol.

### Plan (depends on P1.B view-tracking)

**Phase 2.A — Reach the byte-OR fast-path from a symbol**:
- Once P1's view-tracking map exists, a loop variable `c` bound from
  `s.data[i]` is recognised as a 1-char view of `s` at offset `i`.
- The `c in valid_chars` comparison resolves via byte-level OR through
  the view-source.
- No `cprover_string_contains_func` call, no SAT-loop.

**Phase 2.B — Unroll symbolic-string for-loop in genexp**:
- Re-add the symbolic-string branch to `try_builtin_call` for
  `all`/`any`:
  ```cpp
  if(is_python_string_type(iterable.type())) {
    for k in [0, MAX_UNROLL):
      in_range = k < length
      c = view{1, s.data + k}    // tracked via P1.A
      eval body with c
      result = and/or with in_range guard
  }
  ```
- Set `MAX_UNROLL = 16` (matches refined-string default size; bounded
  so we don't blow up SAT).

**Phase 2.C — Static body audit**:
- Before unrolling, walk the body AST for any operation that lowers
  to `cprover_string_*` (refined-string ops): `find`, `replace`,
  `startswith`, etc. on symbolic strings.
- If found, fall back to nondet (don't unroll). The byte-OR fast-path
  covers the common case (`c in const`, `c == const`, `c.isXXX()`).

### Expected gains
- `github_3036`, `github_3036_6` (hex/letter validation)
- `type-inference-for-len` (parenthesization checking)
- A few smaller tests in the symbolic-string-iter cluster

### Risks
- Refined-string solver crash if the body audit misses an operation.
- Unrolling 16 iterations × heavy body could bloat SAT.

---

## P3 — Bare `list` annotation as `list[Any]`

### Root cause

`def f(items: list)` — bare `list` annotation maps to
`python_list_type(python_int_type())`. This forces string elements at
the call site to be erased to ints. PLR-correctly, bare `list` is
`list[Any]` — should be `python_list_type(python_value_type())`.

We tried this change and `match-sequence-mapping` regressed. We
didn't dig into the cause.

### Plan

**Phase 3.A — Diagnose `match-sequence-mapping` regression**:
- Save a CSV diff: pre-change vs post-change, narrow to that single
  test.
- Read the test (`regression/python/match-sequence-mapping/main.py`)
  and the regression failure message (assertion vs exception vs
  verification).
- Trace the GOTO before/after. Likely the match pattern unification
  loses precision when element type widens to `python_value`.

**Phase 3.B — Conditional widening**:
- If the regression is about list[Any] losing element-type info,
  introduce a heuristic: keep `list[int]` for bare `list` *only* when
  the value at the assignment/parameter site is a list of int
  constants. Use `list[Any]` for cross-function arguments (where the
  caller may pass anything).
- Alternative: apply `list[Any]` only at parameter annotation sites,
  not at variable annotations.

**Phase 3.C — Match-pattern element-type inference**:
- When converting a match pattern that decomposes a list, infer the
  element type from the pattern's sub-patterns. E.g.,
  `case [int(x), int(y)]:` constrains element type to int regardless
  of the annotation.
- This is the most general fix; addresses other potential issues with
  match patterns over heterogeneous lists.

**Phase 3.D — Land**:
- Run all three regression suites + ESBMC sweep.
- Expected closures: `github_3433` (`def check(items: list) -> bool:
  return all(isinstance(x, str) for x in items)`).

### Expected gains
- `github_3433` (~+1)
- A few similar bare-list tests (~+1–2)

### Risks
- `match-sequence-mapping` and possibly other match-pattern tests.
- Lower priority than P0–P2.

---

## Suggested execution order

1. **P0 first** — it's the largest investment but it's a *correctness*
   fix, not a workaround. Many of our current point-fixes
   (`is_optional_sym` skip, length-0 Optional[str] marker,
   struct-vs-sentinel False fast-path) become unnecessary once None
   has its own type. The codebase gets simpler as a side-effect.

2. **P1 next** — moderate effort, large test gain in a clearly-bounded
   area (string-symbolic cluster). Independent of P0.

3. **P2 after P1** — directly enabled by P1's view-tracking
   infrastructure; without P1 the byte-OR fast-path can't fire from a
   symbol-bound loop variable.

4. **P3 last** — small gain, but quickest to ship once we find time.

If starting: kick off P0 with the audit phase (Phase 0.A) first —
that gives concrete data on the size of the change before committing
to it.

---

## Cross-references

- Roadmap snapshot: `doc/python-frontend-roadmap.md` (status table +
  per-cluster history).
- Architecture overview: `doc/python-frontend-architecture.md`.
- Related deferred items (not blocked, just lower-priority):
  - Class attribute fallback (class10/11/12, class-attributes,
    class-attributes_fail) — separate cluster, similar effort tier
    to P0.
  - Higher-order function calls (lambda5, lambda12, lambda18, lambda19,
    callable4, higher-order2) — needs first-class function support.
  - Heterogeneous dict.values() iteration with NaN
    (github_3719_4-nondet, github_3719_5-nondet) — model decision
    about constraining `nondet_float()`.

---

**Last updated:** 2026-05-29 (PASS 2851 cumulative wave 41).
