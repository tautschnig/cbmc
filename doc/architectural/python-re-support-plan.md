\file
# Supporting the `re` module for Python verification

This is a design note for adding proper support for Python's `re`
regular-expression module. `re` support is listed as a Step 4
candidate in `doc/architectural/python-module-support-plan.md`;
because it touches solver integration in addition to the front-end,
it deserves a dedicated plan rather than a spontaneous
implementation.

## Motivation

`re` shows up everywhere in real Python code — URL validators,
configuration parsers, log processors, CSV dialect detection — and
the AWS SDK code that dominates our benchmark corpus uses it
routinely. ESBMC's Python front-end reports eleven wrong-fail
cases that bottom out in `re` usage, so getting this right is a
precision improvement for anyone verifying typical Python programs.

## Current state

The front-end has an ad-hoc fallback for a handful of method names
(`search`, `match`, `compile`, `findall`, `sub`, `split`) that
returns a fresh non-negative nondet value. This lets callers write
`if re.search(...) is not None` without losing all paths, but it
does not reason about the pattern at all — `re.search('^http://',
url)` and `re.search('^xyzzy$', url)` are indistinguishable.

## What we would like

1. **Sound**: a program that passes verification under the model
   must actually be correct for all inputs the real `re` would
   allow.
2. **Useful**: the model should be strong enough that typical
   regex-based input validators can be proved correct or fail with
   a concrete counter-example.
3. **Backend-portable**: work with both the default string-
   refinement loop and `--cvc5`. It is acceptable for one backend
   to be more precise than the other; it is not acceptable for one
   backend to report UNKNOWN where the other reports SUCCESS.

## Options considered

### Option A — shallow Python stub

Replace the ad-hoc frontend handling with a proper
`src/python/library/re/__init__.py` that exposes `Match`, `Pattern`,
`re.match`, `re.search`, `re.sub`, `re.findall`, `re.compile`, etc.
Each returns a nondet `Match` object (or `None`) with the
attribute shape callers rely on (`group()`, `groups()`, `start()`,
`end()`, `span()`, `groupdict()`).

This is exactly what we've done for `urllib.parse`. It is cheap,
entirely Python-side, and does not touch the solver at all.

* **Pros**: trivial; no solver changes; works identically across
  backends; preserves soundness (nondet is the widest legal
  over-approximation).
* **Cons**: gives no precision over the current ad-hoc handling.
  Cannot prove `re.search('^http', 'http://…')` is truthy even
  though that is an elementary property. Every regex-gated branch
  remains effectively nondet.

### Option B — SMT string theory (via `--cvc5`)

SMT-LIB 2.6 defines a theory of strings with a sub-theory for
regular expressions: `str.in_re`, `re.++`, `re.union`, `re.inter`,
`re.*`, `re.+`, `re.?`, `re.range`, `re.loop`, `re.comp`,
`str.to_re`, `str.replace_re`, `str.replace_re_all`. CVC5
implements this theory.

The plan would be:

1. In the front-end, lower a `re.match(pattern, string)` call with
   a **compile-time constant** `pattern` into a call of a new
   intrinsic, e.g. `cprover_string_match_func(string, pattern)`.
   The pattern is kept as a literal string on the intrinsic; we do
   not translate the regex at the front-end level.
2. In `smt2_conv.cpp`, intercept that intrinsic (the way we already
   intercept `cprover_string_equal_func`) and emit
   `(str.in_re <string> <regex>)`, where `<regex>` is the SMT-LIB
   translation of the Python pattern. This translation happens in
   the SMT back-end where we already know we target CVC5.
3. For patterns that are not compile-time constants, fall back to
   Option A's nondet `Match` — dynamic regex synthesis is a
   research-grade problem we should not take on here.

Python-to-SMT-LIB regex translation is non-trivial but well-scoped:

* Character classes (`[a-z]`, `[^0-9]`, `\d`, `\w`, `\s`) map to
  `re.range` / `re.union`.
* Quantifiers: `*` → `re.*`, `+` → `re.+`, `?` → `re.?`, `{m,n}` →
  `re.loop`.
* Alternation `|` → `re.union`.
* Anchors `^`/`$` require care: `re.match` is anchored at the
  start, `re.search` is not, `re.fullmatch` is anchored at both
  ends. We encode this as
  `(str.in_re s (re.++ ... re.all ...))` as appropriate.
* Back-references (`\1`, `\2`) are **not** expressible in SMT-LIB
  regex theory — any pattern containing them falls back to Option
  A's nondet `Match`.
* Named groups / captures require extra plumbing; treat `group()`,
  `groups()`, `span()` etc. as nondet for now and plan group
  support as a later extension.

* **Pros**: gives real precision to programs that use `re` with
  constant patterns (the common case); scales to the full Python
  regex language except for back-references and captures;
  reuses the sound CVC5 integration.
* **Cons**: the Python-to-SMT-LIB translator is new code (~300–500
  lines) and needs its own test suite; regex performance in CVC5
  can be unpredictable — a small regex can cause a slow solve; only
  helps when the user runs `--cvc5`.

### Option C — regex axioms in the native string refinement loop

The existing string refinement loop (used by the default back-end)
already has axioms for `equal`, `concat`, `length`, `substring`,
`contains`, `index_of`, `replace`. Adding `str.in_re` would mean
implementing the standard axioms for regex membership (Blanchette &
Ouimet, Abdulla et al.) inside `src/solvers/strings/`.

* **Pros**: benefits every back-end, including the default one;
  keeps behaviour identical across solvers; MiniSAT-friendly.
* **Cons**: a substantial research-level piece of work. Would also
  shift the string refinement loop from a pure linear-arithmetic-
  plus-uninterpreted-functions model to one that reasons about
  NFA/DFA inclusion, which is a very different algorithmic beast.

## Recommended plan

Do it in three waves:

1. **Wave 1 — Option A (Python stub):** ship
   `src/python/library/re/__init__.py` with a complete stub of the
   public `re` API (`Match`, `Pattern`, `match`, `search`, `sub`,
   `findall`, `split`, `compile`, `escape`, `fullmatch`). Retire
   the ad-hoc special-case in the front-end once the stub is in
   place. This gives us a consistent story, backend-agnostic, with
   no precision regression vs. today.

2. **Wave 2 — Option B (SMT regex for `--cvc5`):** add a
   `cprover_string_match_func` / `cprover_string_search_func` /
   `cprover_string_fullmatch_func` intrinsic. Implement the Python
   → SMT-LIB regex translation in a new header
   `src/solvers/strings/python_regex_to_smt.{h,cpp}`. Intercept the
   intrinsic in `smt2_conv.cpp` the way `cprover_string_equal_func`
   is intercepted today. Update the Python library's `re` stub so
   that when the pattern argument is a literal, it emits the
   intrinsic; otherwise it falls back to Wave 1's nondet path.

   Exit criterion: at least one program in
   `regression/python/` demonstrates verifying a property that
   depends on a constant-pattern regex with `--cvc5`, and the same
   program is provably nondet with the default back-end.

3. **Wave 3 — Option C (native regex axioms), as-and-when:** treat
   this as an open research item rather than a roadmap commitment.
   Revisit only if Wave 2 proves insufficient for benchmark
   precision and the cost/benefit makes sense. Candidate starting
   point: decision procedures for regex constraints on strings
   (Liang et al., CADE 2014) as already used in CVC5.

## Out of scope for this plan

* Back-references, lookahead/lookbehind, possessive quantifiers —
  not expressible in SMT-LIB regex theory. Keep as nondet.
* Precise modelling of `re.sub` with replacement back-references.
  The return value is a nondet string.
* Precise capture groups (`Match.group(1)` etc.). The group values
  are nondet strings satisfying the obvious length constraints
  (0 ≤ `len(group(i))` ≤ `len(subject)`).

## Open questions

* How do we surface regex-syntax errors at translation time without
  crashing? A malformed Python pattern should fail at front-end
  time, but CBMC is not a regex checker. Probably: translate
  errors downgrade to the Wave 1 nondet fallback with a
  log_overapprox warning.
* Do we want `re.IGNORECASE` / `re.MULTILINE` / `re.DOTALL`? SMT-LIB
  regex has no flag concept. Each flag would require rewriting the
  regex AST (case-folding character classes for IGNORECASE,
  inserting `\n` handling for MULTILINE/DOTALL). Start with
  flag-free, flag-present falls back to Wave 1.

## Review request

Please review this plan — in particular Wave 2's translation
effort and the fallback strategy for unsupported regex features —
before we invest implementation time.
