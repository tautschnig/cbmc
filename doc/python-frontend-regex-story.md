# Regex (`re` module) support — current state

A snapshot of how CBMC's Python frontend models the `re` module
as of 2026-05-27 (wave 40). For the long-term plan see
[python-frontend-plans.md §4](python-frontend-plans.md#regex).

## TL;DR

| Backend | Pattern / subject shape | Behaviour |
|---|---|---|
| Default (refine-strings) | constant pattern + constant subject | Constant-fold via the back-end's regex preprocessor (limited cases). Otherwise a sound nondet over-approximation. |
| Default | constant pattern + symbolic subject | Sound nondet — the backend can't reason over the pattern. |
| `--cvc5` | constant pattern + constant or symbolic subject | SMT-level `str.in_re` reasoning via the `cprover_string_{match,search,fullmatch}_func` intrinsics, when called via `__cbmc_re_{match,search,fullmatch}`. |
| `--cvc5` | symbolic pattern | Sound nondet — patterns are kept as literal strings on the intrinsic, so a symbolic pattern reduces to nondet. |
| Anywhere | dynamically-built pattern | Sound nondet. |

Today's coverage is enough that **most regex-gated branches stay
reachable** (no false dead-code), but **most regex-content
properties don't prove**. The session work has been on
soundness, not regex precision.

## What's modelled

### Library stub (`src/python/library/re/__init__.py`)

A 190-line shallow stub that mirrors the public surface of `re`:

- `re.match`, `re.search`, `re.fullmatch`, `re.sub`, `re.subn`,
  `re.findall`, `re.finditer`, `re.split`, `re.escape`,
  `re.compile`, `re.purge`.
- `Pattern.match`, `.search`, `.fullmatch`, `.sub`, `.subn`,
  `.findall`, `.split`, `.finditer`.
- `Match` object with `group()`, `groups()`, `start()`, `end()`,
  `span()`, `groupdict()`, `pos`, `endpos`, `lastindex`,
  `lastgroup`.
- Compilation flag constants (`IGNORECASE`, `MULTILINE`, …).
- `re.error` exception class.

Each function returns a sound nondet shape: `match` returns
`Match | None`, `findall` returns a `list[str]`, `split` returns
a `list[str]`, etc. The stub does NOT reason about the pattern.

### Frontend hooks

Three undocumented user-facing builtins exist for direct SMT
regex testing:

- `__cbmc_re_match(pattern, subject)` → bool
- `__cbmc_re_search(pattern, subject)` → bool
- `__cbmc_re_fullmatch(pattern, subject)` → bool

These lower to `cprover_string_match_func` /
`cprover_string_search_func` / `cprover_string_fullmatch_func`
intrinsics (see `src/python/python_converter_call.cpp` ~line 4713).
The intrinsics carry the pattern as a literal string. CBMC's
SMT2-conv layer intercepts them and emits `str.in_re` with the
translated regex when the backend supports it (CVC5).

The library wrapper `re.match(p, s)` does **not** yet route
through this intrinsic — there's a documented "subject-at-SMT-
time integration" follow-up ("the 'a-prime' refactor") that would
propagate literal args through the library body. Without that,
calling the library `re.match` keeps the shallow stub behaviour;
direct calls to `__cbmc_re_match` get the SMT precision.

### Stage-1 regex-no-match precision check

A frontend-level precision tactic exists for one specific
recurring shape: in a stub method body that contains an
assertion of the form

```python
assert compile(<literal>).search(kwargs[K1][K2]...) is not None
```

we walk the kwarg-path and the user's call site to see whether
the value reaches a constant `""` (empty string). If the regex
can't accept ε (the empty word) we emit a regex-no-match
property at the call site. This caught a handful of stub-driven
boto3 issues without touching the solver.

(See `method_regex_asserts` in `python_converter_defs.cpp` and
the corresponding consumer in `python_converter_call.cpp`'s
kwarg processing.)

## What works today

- **`re.search(pattern, s) is not None` truthiness paths** —
  both branches reachable, callers that branch on regex success
  get full path coverage.
- **`Match` object shape** — `m.group(0)`, `m.span()`, `m.start()`,
  etc. all return typed nondets, so callers that consume match
  metadata don't crash on missing-attribute errors.
- **Regex API surface** — code that imports `re` and uses any
  documented function compiles and runs through verification
  without unresolved-symbol errors.
- **`__cbmc_re_match(literal, literal)`** under `--cvc5` — exact
  match decision via SMT regex.
- **Stage-1 regex-no-match check** — false-attribute-shape
  detection on stub-recorded patterns.

## Update (2026-06-14): library routing + soundness hardening

Two things changed since the snapshot above:

- **`re.*` now routes through the SMT decision** (the "a-prime" refactor,
  commits `3e2888f01f` / `5231f3b61d`). `re.match`/`re.search`/`re.fullmatch`
  (and the `Pattern` methods) return a real `Match`/`None` derived from
  `__cbmc_re_*`, so a supported constant pattern is precise via the library
  wrappers, not just the direct intrinsics — under `--cvc5` for both constant
  and symbolic subjects.
- **The shared translator was audited and hardened for PLR soundness**
  (commit `517d3194f4`). It feeds every precise regex decision, so its bugs
  were group-wide. Fixed: `^`/`$` anchors were stripped then ignored (so
  `re.search("^abc", s)` matched "abc" anywhere); `.` matched `\n`; negation
  (`[^…]`, `\D`, `\S`, `\W`) used a bare `re.comp` whose language also contains
  `""` and multi-char strings; control-character escapes emitted a literal
  `\n` instead of the `\u{a}` code point; `\A`/`\Z` degraded to literals;
  `{m,n}` with `n<m` emitted a degenerate loop. Anchoring is now a unified
  effective-anchor model that also captures the non-MULTILINE rule that `$`
  matches before a single trailing newline.
- **Untranslatable/symbolic patterns are now nondet, not "no match"**
  (commit `944e020463`). Because the stub routes through the intrinsic, the
  previous definite `bv0` fallback became an always-`None` false proof; it is
  now a fresh nondet so both branches stay reachable.

The "What doesn't work" list below still holds for **substitution, group
extraction, split/findall, symbolic patterns, and compilation flags** — those
are precision features, not soundness gaps.

## What doesn't work


- **Pattern semantics under the default backend.** Without
  `--cvc5`, `re.match("^abc$", "abc")` is nondet — same answer
  as `re.match("^xyz$", "abc")`.
- **Library-routed regex precision.** `re.match("abc", "abc")`
  via the library stub goes through the shallow nondet model
  even when called with constant pattern and subject. The
  `__cbmc_re_*` direct intrinsics work, the `re.*` wrappers
  don't.
- **Group extraction.** `m = re.match(p, s); m.group(1)` returns
  a nondet string regardless of the pattern's groups.
- **Substitution.** `re.sub(p, repl, s)` returns a nondet string
  even for fully constant inputs.
- **Symbolic patterns.** `re.match(some_var, s)` is always nondet
  (we don't have an SMT regex of the symbolic pattern).
- **Compilation flags.** `re.IGNORECASE`, `re.MULTILINE`, etc.
  pass through but don't influence the SMT translation.

## Test coverage

7 dedicated `re` tests in `regression/python`:

| Test | Purpose | Status |
|---|---|---|
| `re-module-basic` | shallow stub doesn't crash | PASS |
| `re-search-non-string-typeerror` | TypeError on non-string subject | PASS |
| `re-wave2-cvc5` | direct `__cbmc_re_*` under `--cvc5` | PASS |
| `re-wave2-intrinsic` | direct `__cbmc_re_*` lowering | PASS |
| `regex-no-match-empty-subject` | Stage-1 precision check | PASS |
| `regex-no-match-no-fp` | no false positive when pattern accepts ε | PASS |
| `regex-no-match-via-nested-kwargs` | Stage-1 with kwarg-path | PASS |

Plus indirect coverage in the AWS benchmark suite (any test
that uses `re` exercises the stub).

## Roadmap

The full plan is in
[python-frontend-plans.md §4](python-frontend-plans.md#regex).
Three design options are documented:

- **Option A** — shallow Python stub (current state for the
  `re.*` wrappers).
- **Option B** — SMT string theory via `--cvc5` (current state
  for `__cbmc_re_*` direct calls; not yet routed through the
  library).
- **Option C** — Python-level pattern interpretation. Less
  ambitious than B but more precise than A.

The most actionable next step is the **subject-at-SMT-time
refactor** (mentioned as "the 'a-prime' refactor" in
`re-wave2-cvc5/main.py`): rewrite the library stub so
`re.match(literal_pattern, subject)` propagates the literal
pattern through to the SMT-conv layer. This unlocks Option B
for the entire `re.*` surface, not just the direct intrinsics.

Estimated effort for the refactor: ~1 week. Estimated impact:
significant precision improvements on real-world Python code
that uses `re` for input validation. The 11 wrong-fail cases
in the AWS benchmark suite that bottom out in `re` usage would
likely close.

## Backend portability

The current implementation is sound on every CBMC backend:

- **Default (MiniSat + refinement strings)**: shallow stub
  reasoning; nondet for everything regex-content-dependent.
- **CaDiCaL**: same as default.
- **CVC5**: direct `__cbmc_re_*` intrinsics get SMT regex
  precision; library stub still shallow.
- **Z3**: direct intrinsics fall through to nondet (Z3's
  string theory is supported by CBMC but `cprover_string_match_func`
  isn't intercepted there).

For users wanting maximum precision today: use `--cvc5` AND
call `__cbmc_re_match` / `__cbmc_re_search` /
`__cbmc_re_fullmatch` directly instead of the `re.*` wrappers.

For most users: the default backend is fine; regex-gated paths
stay reachable, content-of-match properties just don't prove.
