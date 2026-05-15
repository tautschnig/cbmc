\file
# Plan: precise `re` for the sagemaker_labeling_job benchmark

This is a design note for the `sagemaker_labeling_job` benchmark
MISS, and for the broader question of how far we should push
regular-expression precision.

It builds on `python-re-support-plan.md` (Wave 1 + Wave 2 design)
and on the comments in `src/python/library/re/__init__.py` that
explain the current `Pattern.search/match/fullmatch` always-Match
return value.

## The bug pattern

```python
self.sagemaker_client.create_labeling_job(
    LabelingJobName=job_name,
    ...
    HumanTaskConfig={
        'WorkteamArn': workteam_arn,
        'PreHumanTaskLambdaArn': '',                            # bug: empty
        'AnnotationConsolidationConfig': {
            'AnnotationConsolidationLambdaArn': '',             # bug: empty
        },
        ...
    },
    ...
)
```

The SageMaker stub asserts:

```python
assert compile(
    "^arn:aws[a-z\\-]*:lambda:[a-z0-9\\-]*:[0-9]{12}:function:"
).search(kwargs["HumanTaskConfig"]["PreHumanTaskLambdaArn"]) is not None
```

The empty string can never satisfy a regex with a non-empty
literal prefix, so `search` should return `None`, the assertion
should fail, and the bug should be detected.

## What the front-end does today

* `Pattern.search/match/fullmatch` in
  `src/python/library/re/__init__.py` call the front-end-special
  `__cbmc_re_match` / `__cbmc_re_search` / `__cbmc_re_fullmatch`
  intrinsic and **discard** the return value, unconditionally
  returning `Match()`. The body comment explains: "the precise
  'Match or None' semantics would require the caller to handle a
  potentially-nondet None and that breaks the common
  `if pat.search(s) is not None: ...` idiom".
* `convert_call` lowers the `__cbmc_re_*` intrinsic to
  `cprover_string_{match,search,fullmatch}_func` provided both
  arguments are `python_string_type`. The cast `to_str` packages
  the args as `{length, data}` struct exprts.
* `smt2_conv.cpp` has interception for these intrinsics:
  * Extracts the **constant** pattern literal.
  * Translates the pattern via
    `python_regex_to_smt_{match,search,fullmatch}` (handles
    character classes, quantifiers, alternation, grouping,
    anchors; rejects back-refs, lookaround, named captures).
  * If the **subject** is also a constant printable-ASCII
    literal, emits `(ite (str.in_re "<subj>" <re>) bv1 bv0)`.
  * Otherwise falls back to `bv0` (sound: "no match").

So with `--cvc5` and a constant pattern + constant subject, the
solver answers precisely. With a symbolic subject, the
interception falls back to bv0 — but the library throws this
result away, so the always-Match return masks the precision.

## What's missing for sagemaker_labeling_job

Two distinct gaps, each of which independently would solve the
benchmark and each of which is independently useful:

### Gap 1 — library wrapper doesn't propagate the intrinsic result

`Pattern.search` returns `Match()` regardless of what the
intrinsic returns. The doc-claimed "wrapped as `Match() or None`"
behaviour was either backed out or never landed. Re-introducing
it under the default backend breaks the `if search() is not
None` idiom for symbolic subjects (intrinsic returns nondet bool,
search returns nondet None or Match, downstream `.group()`-style
code blows up).

### Gap 2 — symbolic-subject precision under `--cvc5`

The smt2_conv interception falls back to bv0 (no match) for
symbolic subjects because the subject is a Python refined-string
struct, not an SMT `String`. The "(a')" architectural refactor —
exposing python_string-typed values to the SMT backend as
`String` — is the gating piece.

For a benchmark like sagemaker_labeling_job, both gaps need to
close. If gap 1 closes alone (return None on bv0), every
symbolic-subject case under `--cvc5` returns None, which is
sound but FPs every benchmark that does
`m = re.search(p, s); m.group(0)` when the regex actually
always matches at runtime.

## Approaches

### A — Library wrapper conditional, gated by a new flag

Add `--python-strict-re-result`. Under that flag, switch
`Pattern.search/match/fullmatch` to:

```python
def search(self, string, pos=0, endpos=0):
    if __cbmc_re_search(self.pattern, string):
        return Match()
    return None
```

Without the flag, keep the always-Match() default.

Pros:
* Trivial library change (4 lines per method).
* Opt-in: existing benchmarks don't regress.
* Catches sagemaker_labeling_job under
  `--python-strict-re-result --cvc5`: the symbolic subject case
  falls back to bv0 (no match), search returns None, the assert
  fails, bug detected.

Cons:
* Default backend (no `--cvc5`): the intrinsic returns nondet
  bool, so search can return None nondeterministically. This
  causes spurious None-deref FPs on programs that work in
  practice but don't check the return.
* Doesn't help the "constant subject" case any better than the
  existing interception already does.

### B — Per-call value propagation: detect provably-no-match cases at the call site

At each `Pattern.search/match/fullmatch` call site (and
similarly for `re.search/match/fullmatch`), if both:
1. The pattern is a constant non-empty regex literal whose AST
   doesn't accept the empty string (we already have
   `python_regex_to_smt_{match,search}` — extend with a small
   "accepts empty?" predicate), AND
2. The subject is statically the empty string,

then emit a `regex-no-match` property at the call site:

```
ASSERT false, comment: "regex '<pattern>' cannot match empty string"
```

The library still returns `Match()` so downstream
`.group(0)` chains aren't perturbed. The caller's
`is not None` may still appear to succeed because the assertion
fires regardless of what `search()` returns.

Detecting "subject is statically empty" requires a small
extension of `dict_literal_value_categories` to also record
constant string values, plus propagation through nested dict
subscripts (one or two levels suffice for the boto3-stub
patterns).

Pros:
* No FP risk: only fires when we can prove the regex won't match.
* Backend-agnostic: works under default and `--cvc5`.
* Reuses existing infrastructure: regex-to-SMT translator just
  needs the small "accepts empty?" predicate.
* Targeted precisely at the sagemaker_labeling_job pattern.

Cons:
* Limited reach: only catches cases where the subject is
  statically the empty string at the call site or in a finite-
  depth dict literal feeding through `**kwargs`. Doesn't catch:
  * Subjects we know are short but non-empty
    (`"a"` against `^arn:`).
  * Subjects we know are bounded but variable.
  * Subjects flowing through user-defined helpers (the same
    Any-erasure problem from the bedrock plan).
* Needs the inter-procedural value propagation through `**kwargs`
  — same plumbing item B in the benchmark issues doc has been
  asking for.

### C — Full Wave-2 completion: SMT String for symbolic subjects

This is the architectural refactor referenced in
`python-re-support-plan.md`'s status note: expose
`python_string_type` values to the SMT backend as `String` so the
interception can emit `(str.in_re subject <regex>)` for symbolic
subjects too.

The cleanest version of this work places the bridging in the
SMT back-end, NOT in the front-end. The front-end keeps
emitting refined-string-typed arguments to
`cprover_string_match_func` exactly as it does today; the
back-end's interception in `smt2_conv.cpp` is the layer that
knows about SMT-LIB's `String` theory and is therefore the
right place to translate refined-string struct values into
String-typed SMT terms.

Layered options:

* C1. Teach `smt2_conv` a generic refined-string → SMT-`String`
  bridge that fires whenever a refined-string struct value
  (constant or symbolic) appears in a position where the SMT
  `String` theory makes sense (regex-membership today; `len()`
  → `str.len`, `+` → `str.++`, slicing → `str.substr` later).
  Big lift; opens the door to broad `--cvc5` precision.

* C2. Specialise the bridge to the regex-intrinsic interception
  only. When `cprover_string_match_func` /
  `cprover_string_search_func` /
  `cprover_string_fullmatch_func` is intercepted in
  `smt2_conv.cpp`, do the refined-string → `String` translation
  inline:
    1. Allocate a fresh SMT `String` symbol.
    2. Emit `(assert (= (str.len smt_str) struct.length))`
       constraint binding its length to the struct's length
       field.
    3. For each `i` in `0 .. PYTHON_MAX_STRING_LENGTH-1`, emit
       conditional constraints binding the i-th character of
       the SMT `String` to the i-th byte of the
       struct's data array (when `i < struct.length`).
    4. Use the SMT `String` symbol in the
       `(str.in_re ... <regex>)` clause.
  This is contained to one site in `smt2_conv.cpp`. No
  front-end changes. No new types.

* C3. Introduce a dedicated `compiled_regex_typet` for
  pre-compiled regexes (analogous to `refined_string_typet`).
  The intrinsic's first argument changes from a refined-string
  carrying the literal pattern to a `compiled_regex_typet` that
  embeds the pattern. Cleaner type signature, but doesn't help
  the symbolic-subject problem on its own; the subject is
  still a refined-string and still needs C1 or C2 to be
  exposed to the SMT backend.

(The earlier draft of this document proposed materialising the
SMT String value in the front-end; that breaks the
front-end / back-end separation we maintain elsewhere and is
discarded.)

Pros:
* Generalises beyond `re`: any string operation gets stronger
  reasoning under `--cvc5`.
* Makes the existing Wave-2 work earn its keep — the regex
  translator already builds the right SMT term.

Cons:
* C1 is deep work: ~1500 lines, integration with refined-string
  fallback, careful handling of mixed-typed contexts.
* C2 is moderate (~300 lines) but doesn't solve the broader
  subject-as-String problem.
* `--cvc5`-only; default backend doesn't benefit.

### D — Wave 3: native regex axioms in the string-refinement loop

`python-re-support-plan.md` Option C / Wave 3. Add `str.in_re`
support to `src/solvers/strings/`. Backend-agnostic. Significant
research effort (Liang et al. 2014 et seq.). Defer to research
mode.

## Recommended plan

Land in three stages.

### Stage 1 — Approach B, contained and immediate

Implementation:
1. Extend `python_regex_to_smt_*` (or add a new sibling) with a
   pure-Python-side predicate `python_regex_can_match_empty(p)`
   that returns true iff the regex has an alternation branch all
   of whose elements are either ε-accepting (`*`, `?`,
   `{0,m}`, alternation containing ε, etc.) or empty.
2. Extend `dict_literal_value_categories` to a richer
   `dict_literal_field_props` map carrying optional constant-
   string values per field, populated from the same AST walk
   already running today.
3. Plumb constant-string propagation through one level of
   subscript: `dict_literal[K]` where K is a constant string
   returns the recorded constant if any.
4. At `Pattern.search/match/fullmatch` (and module-level
   variants), when both (a) the pattern is a constant string
   literal that the regex translator accepts, (b) the
   regex-can-match-empty predicate returns false, and (c) the
   subject is statically the empty string (directly, or via
   one-level dict subscript on a tracked dict literal):
   * Emit a `regex-no-match` property at the call site with a
     descriptive comment.

Cost: ~250 lines + 3-4 regression tests.

Benchmark impact: catches sagemaker_labeling_job (both bugs).
No FP risk because the assertion only fires when we can prove
the regex genuinely can't match.

This is the right first landing. Item B (inter-procedural value
propagation through kwargs) gets a concrete payoff alongside.

### Stage 2 — Approach A, gated

Add `--python-strict-re-result` for users who want strict
modelling. Document that it's expected to interact with `--cvc5`
to deliver value (under default backend it tends to FP).

Cost: ~30 lines + 2 regression tests.

### Stage 3 — Approach C

Pursue C2 (back-end-side refined-string → `String` bridge,
contained to the regex-intrinsic interception in
`smt2_conv.cpp`). Strict architectural rule: NO front-end
emission of SMT-String-typed values. The front-end emits
refined-string arguments exactly as today; the back-end alone
knows about SMT-LIB `String` semantics.

Cost: ~250 lines in `smt2_conv.cpp`. Defer C1 (broader
refined-string-as-SMT-String integration) to a separate plan.

### Wave 3 — Defer

Treat Approach D / Wave 3 as a research item, not a roadmap
commitment.

## Stage 1 detailed plan (the actionable piece)

### 1.1 Add `python_regex_can_match_empty`

In `src/solvers/strings/python_regex_to_smt.{h,cpp}`:

```cpp
/// Return true iff the regex `pattern` can match the empty
/// string. Conservative: returns true (don't fire) when the
/// pattern uses a feature we don't analyse (back-refs, etc.).
/// Examples:
///   "^foo$"           → false
///   "^a*$"            → true
///   "^(a|b*)$"        → true (b* accepts empty)
///   "^a+(b)?$"        → false (a+ requires at least one a)
///   "^"               → true
///   "^arn:..."        → false (literal arn: prefix is required)
bool python_regex_can_match_empty(const std::string &pattern);
```

The implementation walks the regex AST (the same AST the existing
translator builds). Each AST node has a known `accepts_empty`:

* literal char/class: false
* `*`, `?`: true
* `+`: child.accepts_empty
* `{m,n}`: m == 0 || child.accepts_empty
* concat: every child accepts_empty
* alternation: any child accepts_empty
* group: child.accepts_empty
* anchors `^`, `$`: true
* dot `.`: false (matches any char, but requires one char)

For `re.search` semantics specifically, the question is "does the
regex accept the empty string anywhere in the subject" — for an
anchored pattern, the same as match. For a non-anchored pattern,
the answer is also "any branch can match empty" (search would
trivially match at position 0).

Re-export the AST walk; ~80 lines.

### 1.2 Track constant-string values in dict literals

Extend `dict_literal_value_categories` to also carry per-key
constant string values. Two implementations:

Option i (simplest): a parallel
`dict_literal_value_string_consts` map of the same shape, only
populated for keys whose value is a `Constant(str)` AST node.

Option ii (cleaner): replace the category map with a richer
`struct {std::string category; std::optional<std::string>
const_str;}` and update consumers.

Choose Option i to keep the diff small.

```cpp
/// Per-dict per-key constant string value, captured from the AST
/// at assignment time. Subset of dict_literal_value_categories
/// where the value AST is a Constant(str).
std::map<irep_idt, std::map<std::string, std::string>>
  dict_literal_value_string_consts;
```

Populate alongside `dict_literal_value_categories` in
convert_assign / convert_ann_assign.

### 1.3 Propagate through one-level dict subscript

In convert_subscript's dict-literal-resolution path, when
returning `vals_arr.operands()[idx]`, also remember the constant
string by attaching a side-channel — actually simpler: the
returned exprt for a constant-string value is already a struct
literal, so `extract_string_value` can recover it. The only
issue is that nested dicts (`dict["A"]["B"]`) need the
intermediate `dict["A"]` to be a tracked dict literal too. Add
a `dict_literal_nested_values` map that carries inner dict
struct exprts where they're known.

For the sagemaker case the nesting is two deep
(`HumanTaskConfig.PreHumanTaskLambdaArn`); start with two-level
support and document one level at a time as we hit benchmarks.

### 1.4 Emit `regex-no-match` at the call site

In convert_call's Attribute branch, when the method is
`search`/`match`/`fullmatch`:

1. Determine the pattern: if the receiver is a `Pattern`
   instance constructed via `compile(p)` with `p` a constant,
   recover `p`. Otherwise, if the call is `re.search(p, s)`
   directly, `p` is the first arg.
2. Determine the subject: if it's a Constant(str), use that. If
   it's a Subscript chain into a tracked dict literal, look up
   the constant.
3. If we have both `p` and a static subject value `s`:
   * Run `python_regex_can_match_empty(p)`.
   * If `s == "" && !python_regex_can_match_empty(p)`: emit
     `regex-no-match` property at the call site.
4. The Pattern.search/match/fullmatch library function still
   returns `Match()` unchanged, so downstream `.group(0)` doesn't
   blow up.

Property class: `regex-no-match`. Not suppressed by
`--python-no-exception-checks` (it's a static bug, not a
runtime exception assertion).

### 1.5 Regression tests

* `regex-no-match-empty-direct`: `re.search("^arn:", "")`
  directly — assertion fails.
* `regex-no-match-via-dict`: empty string in a dict literal,
  read via subscript, fed to search. Mirrors the
  sagemaker_labeling_job pattern.
* `regex-can-match-empty`: `re.search("^a*$", "")` — assertion
  doesn't fire.
* `regex-non-empty-subject`: `re.search("^arn:", "arn:foo")` —
  assertion doesn't fire.

### 1.6 Benchmark verification

Run sagemaker_labeling_job:

```
cbmc --no-unwinding-assertions --unwind 3 \
     --python-no-exception-checks \
     --python-required-kwarg-checks \
     --python-check-typeddict-fields \
     sagemaker_labeling_job.py
```

Expected:
* `regex-no-match` property fires for `PreHumanTaskLambdaArn`.
* `regex-no-match` property fires for
  `AnnotationConsolidationLambdaArn`.
* Verification fails → TP.

Confirm full benchmark suite has no regressions.

## Should we eye full re via the string solver as well as SMT?

**Yes, but as Stage 3 / Wave 3, not the immediate landing.**

The reason: today's blocker for sagemaker_labeling_job isn't
solver capability — it's that the front-end discards the
intrinsic result and that the symbolic-subject case isn't
exercised. Stage 1's call-site assertion approach gives the
benchmark precision today without solver work.

Wave 2 completion (Stage 3 above, Approach C2) is the right
follow-up for general regex precision. It also lights up
non-regex string operations (length, equality, slicing) under
`--cvc5` once the front-end materialises Python-string subjects
as SMT Strings — that has wide payoff beyond `re`.

Wave 3 (native string-refinement axioms for `str.in_re`) would
make precision uniform across backends but is research-grade
work. The 2014 Liang et al. paper and subsequent CADE work give
a starting point but the engineering effort is months, not
days.

Concrete recommendation:

| Stage | Approach | Scope                          | Cost           | Win                                  |
|-------|----------|--------------------------------|----------------|--------------------------------------|
| 1     | B        | sagemaker, similar             | ~250L          | Targeted call-site assertions        |
| 2     | A        | strict mode                    | ~30L           | Optional: True/Match propagation     |
| 3     | C2       | symbolic subjects + cvc5       | ~250L (smt2_conv only) | Wave 2 finishes; precise re for cvc5 |
| 4     | C1       | broader String integration     | ~1500L         | Length, equality, slicing under cvc5 |
| 5     | D / W3   | native regex axioms (refine-strings) | research-grade | Backend-agnostic precision           |

Architecture invariants throughout:

* The front-end emits `cprover_string_match_func` /
  `cprover_string_search_func` /
  `cprover_string_fullmatch_func` with refined-string arguments
  for both the pattern and the subject. No SMT-LIB or
  back-end-specific types ever cross the front-end / back-end
  boundary.
* Per-back-end interception (`smt2_conv.cpp` for SMT-LIB,
  `string_constraint_generator_main.cpp` for the
  string-refinement loop) handles the bridge to that back-end's
  native string representation.
* If finer types prove valuable (e.g. distinguishing pre-
  compiled regexes from raw patterns), they are introduced in
  `src/util/` as new `…_typet` classes alongside
  `refined_string_typet` — never as one-off front-end
  materialisations.

Land Stage 1 next; sequence 2 → 3 as the suite demands; Stage 4
is the standalone "Python-strings-as-SMT-Strings" plan that
deserves its own design note; Stage 5 is research.
