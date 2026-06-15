# Match-position-returning regex intrinsic — scope & design

Status: **SCOPED (not implemented).** This is the deep-dive linked from
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

- **Phase 1 — positions + `Match` (fixed-length, gated).** The two intrinsics,
  lowering, length/empty gates, and the `Match` plumbing. Precise
  `start/end/span/group(0)` for fixed-length patterns on constant/short
  subjects; sound nondet otherwise.
- **Phase 2 — precise `re.findall` / `re.split`.** Bounded loop in the re stub
  using `__cbmc_re_search_start(..., from)`; `from = end` (or `start+1` for the
  excluded empty-match case). Replaces the nondet-list floor *only* on the
  gated subset.
- **Phase 3 — `Match.group(n)`.** Span + uniqueness-gated `str.++`
  decomposition (plans section 4 group-extraction item).
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
