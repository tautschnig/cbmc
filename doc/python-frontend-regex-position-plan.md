# Match-position-returning regex intrinsic — scope & design

Status: **Phase 1 LANDED (2026-06-15, `1e52da2ca5`); Phases 2-4 PLANNED.**
This is the deep-dive linked from
[python-frontend-plans.md](python-frontend-plans.md) §4 (regex reach). It
designs the primitive that unlocks **precise** `re.findall` / `re.finditer` /
`re.split` enumeration and `Match.start/end/span/group`, and analyses its
soundness and (the dominant constraint) its performance.

## 1. Why this is the high-leverage piece

The current `__cbmc_re_{match,search,fullmatch}` intrinsics lower to a single
`(str.in_re subject <regex>)` — a **boolean** "did it match". Everything that
needs *where* a match is, is therefore blocked on the same missing capability:

| Feature                         | Needs                                  |
|---------------------------------|----------------------------------------|
| `Match.start()/end()/span()`    | match start/end offsets                |
| `Match.group(0)`                | `subject[start:end]`                   |
| `re.findall` / `re.finditer`    | iterate match spans left to right      |
| `re.split`                      | the gaps *between* match spans         |
| `Match.group(n)` (n>=1)         | span + sub-group decomposition         |

So a single position primitive is the shared substrate for the whole cluster —
the architectural "address a group of point fixes" move. Today all of these
are at their **sound floor** (`findall`/`finditer`/`split` return a bounded
nondet list as of `72ef692453`; `Match.*` return constants). This doc is the
path from sound-but-imprecise to precise.

## 2. Why there is no SMT primitive for it

SMT-LIB 2.6 String theory (and CVC5) give us:

- `str.in_re : String x RegLan -> Bool` — membership only, no offset.
- `str.indexof : String x String x Int -> Int` — first index of a **string**
  needle >= `from` (-1 if none). The needle is a literal string, **not** a
  regex.
- `str.substr`, `str.len`, `str.++`, `str.at`, `str.replace_re_all`.

There is **no** `str.indexof_re` / regex-match-position operator. So a regex
match offset must be *encoded*. The hard part is Python's **leftmost-longest**
semantics: "no match begins before the reported start" is a
negation-of-existence over start positions, which is not directly expressible
without quantifiers.

## 3. The encoding: bounded leftmost-start scan (BMC-style)

Because the frontend already bounds strings to `PYTHON_MAX_STRING_LENGTH`
(64), we can replace the quantifier with a **bounded linear scan** over
candidate start positions — exactly the style of the existing 64-unroll
subject bridge in `smt2_conv.cpp`.

For a **fixed-length** pattern of length `L` (we already have
`python_regex_fixed_length(pattern)`), a match begins at position `i` iff
`str.substr(subject, i, L) in <regex-body>`. The leftmost start is the minimum
such `i`, encoded as an `ite` chain:

```smt2
(define-fun m ((i Int)) Bool (str.in_re (str.substr s i L) <body>))
(define-fun start () Int
  (ite (m 0) 0 (ite (m 1) 1 (ite (m 2) 2 ... (ite (m (N-L)) (N-L) (- 1))))))
; end = start + L   (L known at lowering time)
```

`start = -1` means "no match" (Match-or-None). `group(0) = substr(s, start, L)`.
A `from` argument (for findall iteration) just starts the chain at `from`.

Leftmost is *exactly* the minimum, and because the length is fixed,
leftmost-longest collapses to leftmost — so **for fixed-length patterns this is
Python-exact**.

### Spike (CVC5) — correctness & soundness confirmed

Pattern `[0-9][0-9]` (`L=2`):

- Constant subject `"ab12cd"` -> `start = 2`, `substr(s,start,2) = "12"`. OK
  (the leftmost two-digit run).
- **Soundness:** asserting the scan reports a `start` while an *earlier*
  position also matched (`start >= 1 and m(start-1)`) is **UNSAT** for all
  5-char strings — the scan provably never skips an earlier match. OK

## 4. The dominant constraint: performance

The same spike, sweeping a fully-**symbolic** subject of length `Lsub` with an
`(Lsub-1)`-deep scan, forcing a match to exist:

| symbolic subject length | CVC5 time |
|-------------------------|-----------|
| 8                       | 0.14 s    |
| 12                      | 0.73 s    |
| 16                      | 2.5 s     |
| 24                      | 19 s      |
| 64 (full bound)         | **timeout (>60 s)** |

The scan is roughly **exponential** in the symbolic-subject length: each
`str.in_re` over a symbolic `substr` is cheap alone, but the `ite` chain forces
the solver to reason about all framings jointly. Conclusions that shape the
design:

- **Constant / literal subjects fold** (each `str.in_re` on a literal substring
  is decided at preprocessing) -> precise positions are essentially free.
- **Symbolic subjects are viable only when short** (<= ~16 chars in a few
  seconds); the full 64-char bound is infeasible.
- This is the *same class* of cliff documented for per-char string methods and
  for [dict-by-ref string keys](python-frontend-dict-byref-plan.md): symbolic
  strings behind a bounded scan + a downstream query blow up.

So the feature must be **gated**, not unconditional.

## 5. Soundness gates (PLR)

1. **Fixed-length patterns only** for the precise path
   (`python_regex_fixed_length` returns `L >= 1`). Variable-length / greedy
   patterns do **not** get precise positions (see section 7) -> fall back to the
   existing sound nondet floor.
2. **Empty-matchable patterns** (`python_regex_can_match_empty`): `re.findall`
   with empty matches advances by one position per CPython; the precise path
   must either special-case this or stay nondet. Recommend: exclude
   empty-matchable patterns from the precise path initially (nondet floor).
3. **Length gate for symbolic subjects:** because of section 4, the precise scan
   should only be emitted when the subject is a constant/literal **or** its
   length is provably <= a small bound (e.g. a configurable
   `--python-regex-pos-bound`, default ~16). Otherwise -> nondet floor. This
   keeps the analysis from silently hanging; the floor remains sound.
4. The nondet floor in every gated-out case is the already-landed bounded
   nondet list / constant `Match` — never an unsound concrete value.

## 6. Proposed API & lowering

Two intrinsics (scalars are the easiest to thread through `emit_string_*`):

```
__cbmc_re_search_start(pattern, subject, from) -> int   # leftmost start >= from, or -1
__cbmc_re_search_end  (pattern, subject, from) -> int   # = start + L, or -1
```

- New `irep_id`s `cprover_string_re_pos_start_func` / `..._end_func` in
  `irep_ids.def`; front-end emission in `python_converter_call_nondet.cpp`
  alongside the existing `__cbmc_re_*` block (same operand coercion, same
  `is_python_string_type` guard).
- `smt2_conv.cpp` lowering next to the existing match/search/fullmatch block
  (~line 3107): reuse `python_regex_to_smt_body` + `python_regex_fixed_length`;
  if `L` is `nullopt` (variable length) or the subject isn't foldable/short,
  emit the pre-declared nondet (the existing `defined_expressions` fallback).
  Otherwise emit the `ite`-chain `start`, and `end = (+ start L)`.
- Returning two scalars (rather than a struct) avoids new aggregate plumbing in
  the SMT layer; `end` re-emits the same scan or is computed as `start + L`.

## 7. Variable-length / greedy positions — deferred

Exact CPython greedy matching (longest at the leftmost start, with
alternation/backtracking) is genuinely hard to encode soundly: per start
position you'd need the *maximum* `l` with `substr(s,i,l) in body` **and** the
remainder consistent — a nested bounded max-scan whose perf is worse than
section 4 and whose semantics still diverge from Python on pathological
backtracking. **Out of scope for the precise path**; these patterns keep the
sound nondet floor. Document as a known precision gap, not a soundness gap.

## 8. `Match` stub plumbing

Today `re.search()` does `if __cbmc_re_search(p, s): return Match()` and the
`Match` is a flat stand-in (`start()->0`, `group()->""`). To carry real
positions the stub changes to:

```python
def search(pattern, string, flags=0):
    if flags != 0:                      # unchanged sound floor
        return Match() if nondet_bool() else None
    st = __cbmc_re_search_start(pattern, string, 0)
    if st < 0:
        return None
    en = __cbmc_re_search_end(pattern, string, 0)
    m = Match(); m._subject = string; m._start = st; m._end = en
    return m
```

`Match.start()/end()` return `_start/_end`; `span()` the tuple; `group(0)`
`self._subject[self._start:self._end]` (native slice -> `str.substr`). `group(n)`
for `n >= 1` composes with the group-extraction decomposition (plans section 4),
splitting `subject[start:end]`. Whenever the intrinsic is gated out it returns
-1/nondet, so the stub naturally degrades to the sound floor.

## 9. Phasing

- **Phase 1 — positions + `Match` (fixed-length, gated). LANDED
  (`1e52da2ca5`).** The two intrinsics, the smt2_conv bounded leftmost-start
  scan (gated to a fixed-length pattern on a constant subject; emission gated on
  `use_smt_string_native`), and the `Match` plumbing. Precise `start()`,
  `end()`, and `group(0)` for fixed-length patterns on constant subjects;
  sound nondet otherwise. Notes vs the original sketch:
  - `group(0)` slices the **local** subject at search() time and stores the
    resulting substring (`_group0`); slicing a string held in an instance
    attribute is imprecise, so the doc's `subject[start:end]`-in-accessor shape
    was replaced by precompute-then-read.
  - A latent bug was fixed en route: a constant string sliced with a
    non-constant (intrinsic-derived) index wrongly returned the whole string;
    the constant-slice path is now gated on the bounds also being foldable.
  - `group()` *no-arg* is now precise too: the general method-dispatch gap that
    left it nondet (method defaults not filled on optional/union receivers) was
    fixed in `27d677fc91`, see plans
    §[method-default-optional](python-frontend-plans.md).
  - **Open:** `span()==tuple` uses pre-existing tuple equality (elementwise is
    precise). `re.match` positions are not yet wired (only search/fullmatch).
- **Phase 2 — precise `re.findall` / `re.split`. Blockers #1 and #2 RESOLVED;
  now blocked on #3 (library-stub constant-prop); stays at the sound nondet-
  list floor.** The design (a bounded re-stub loop using
  `__cbmc_re_search_start(..., from)` with `from = end`, `start+1` for empty
  matches) is validated end-to-end **in user code** (precise count/elements,
  0 s) — the exact same loop shape, with the subject as a parameter, verifies
  precisely. The blocker chain:
  1. **Empty-list element typing — RESOLVED (front-end, `9173fea6ce`).**
     `result: list[str] = []` pins the element type from the annotation, so the
     loop no longer aborts. The non-annotated `[]` + call-indexed slice append
     is additionally made non-aborting by the back-end defensive net
     (`1c62587a8e`, sound nondet for `smt_string`↔scalar casts), plans
     §[empty-container-elem-type](python-frontend-plans.md).
  2. **Symbolic-`from` lowering — RESOLVED (`bbcb5a3292`).** The lowering now
     accepts a symbolic `from` (guard each scan branch with `i >= from`,
     `let`-binding `from`) while keeping a constant-`from` fast path for
     `from = 0` (search/fullmatch), so there is no perf regression (re-heavy
     tests back at 0 s). Validated on the findall-shaped loop in user code.
  3. **Library-stub subject not constant-folded — RESOLVED (`f0e06d3958`).**
     Root cause: `re.findall` declared `pattern, string` **without** type
     annotations, so the params were any-typed (`python_value`) and the literal
     subject did not fold into the intrinsics — unlike `re.search` in the same
     module (`pattern: str, string: str`) and unlike a user function with the
     identical loop. Annotating the params alone flips it from nondet to
     precise. Found by a minimal probe (no-loop findall returning the intrinsic:
     nondet with bare params, precise with `: str`).

  **`re.findall` is now PRECISE** (`f0e06d3958`) for a fixed-length pattern on a
  constant subject under `--python-smt-strings` (count/elements/indexing exact,
  0 s); unsupported pattern / symbolic subject → sound bounded enumeration; the
  refined backend degrades soundly (slower on a pathological iterate-and-assert,
  but the sweep is neutral). **`re.split` is now PRECISE too** (`dda00127e2`):
  `re.split(",","a,b,c") == ["a","b","c"]`, `split("abc") == ["abc"]`, two-char
  separators, and `maxsplit` all exact for a fixed-length pattern on a constant
  subject; sound nondet otherwise.
  - It was blocked on an imported-module default-fill gap, root-caused via
    `--show-goto-functions`: `re.split(",","abc")` was emitted as
    `CALL split(",", "abc", 0)` — only **3** args, `flags` (index 3) omitted →
    GOTO-nondet, so the `if flags != 0` guard polluted the result. The
    module-call default-fill loop fills trailing params from `default_values`
    and stops at the first gap, and `process_imported_module` populated **no**
    defaults for module-level functions (only class methods got them, via
    defs.cpp). So `re.findall` survived by collision (`Pattern.findall` set
    `{findall,2}={findall,3}=0`, coincidentally matching) but `re.split` did not
    (`Pattern.split` has no index-3). Fixed by populating `default_values` for
    imported module-level FunctionDefs in `process_imported_module` — a
    whole-group fix for every imported module-level function with defaulted
    trailing params (e.g. also `re.sub`'s `count`/`flags`). It was a
    default-fill/dispatch issue, **not** constant propagation.
  - (The earlier "multi-append list-length limitation" was a FALSE conclusion
    from a stale/messy session state — int-list and inline string-list
    multi-append both fold fine.)
- **Phase 3 — `Match.group(n)` for n>=1. SCOPED + spike-validated (2026-06-16);
  not yet implemented.** Extract capture-group text by a `str.++` decomposition
  of the matched span.

  *Encoding (CVC5-spiked, definitive):* segment the pattern into a top-level
  sequence of literal runs and capture groups, e.g. `(\d+)-(\d+)` ->
  `[grp(\d+), lit("-"), grp(\d+)]`. Introduce a fresh `smt_string` `gi` per
  group; assume `matched == seg0 ++ seg1 ++ ...` (literals are constants,
  groups are the `gi`) and `gi ∈ body(sub_pattern_i)` via the existing
  `fullmatch` intrinsic / `str.in_re`; `group(i)` returns `gi`. No greedy
  maximality is asserted.

  *Soundness (verified by spike):* the decomposition **over-approximates** —
  `gi` ranges over every valid split — so it is **sound for all patterns**
  (e.g. `for c in m.group(1): assert P(c)` checks `P` over a superset of
  Python's group). It is **precise exactly when the split is uniquely
  determined**: for `(\d+)-(\d+)` on `"12-34"` the literal `-` pins it and `g1`
  is forced to `"12"` (spike: asserting `g1 != "12"` is UNSAT); for `(\d+)(\d+)`
  on `"1234"` (adjacent variable groups, no separator) `g1` is **not** forced
  (multiple splits) so `group(1)` is a sound nondet-among-valid-splits. No
  uniqueness *gate* is needed for soundness — only for the precision claim.

  *Implementation (the work):* (1) a **top-level pattern segmenter** in
  `python_regex_to_smt` (the translator parses `(...)` but exposes no
  capture-segment list); bail to the sound floor on alternation at top level,
  quantified/nested capture groups, anchors mid-pattern, back-refs. (2) A
  front-end decomposition for `m.group(n)` mirroring the landed `strip(chars)`
  decomposition (introduce `gi`, push `code_assumet`s for the concat + each
  `fullmatch(gi, sub_i)`, return `g_n`); needs the **constant pattern** stored
  on the `Match` (add `_pattern`) and the matched text (`_group0`, already
  stored). (3) `group(n>=1)` currently returns a sound `nondet_str()`
  (`f0e06d3958`) — that stays the floor when the pattern isn't a constant /
  isn't segmentable / `n` is out of range. PLR: groups are leftmost-longest;
  the precise subset (literal-pinned) coincides, the rest stays sound nondet.
- **Phase 4 — variable-length/greedy.** Deferred (section 7); stays nondet.

## 10. Risks / open questions

- **Perf (primary).** The section 4 cliff means the symbolic precise path is
  only ever for short subjects. Needs the length gate + a CLI bound, and a
  regression guard that long-symbolic subjects fall to the floor (not hang).
  Measure the *end-to-end* cost (cbmc, not raw CVC5) before committing — the
  existing 64-unroll bridge already adds overhead.
- **Empty matches** (`re.findall("a*", s)`): semantics + iteration advance;
  excluded from the precise path initially.
- **Interaction with flags** (`re.I` etc.): positions on a flagged pattern need
  the inline-flag prefix folding; until then flagged patterns stay nondet.
- **Two intrinsics vs one struct-returning intrinsic:** scalars are simpler now;
  revisit if `group(n)` wants a richer return.
