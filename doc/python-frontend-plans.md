# CBMC Python frontend — plans & future work

This is the **single** forward-looking backlog for the Python frontend
(`src/python/`). It is the companion to
[python-frontend-architecture.md](python-frontend-architecture.md): every
gap or PLR deviation noted there links to a section here. Each section is
either a **concrete plan** (with a fix shape and scope) or an explicit
**no plan yet**.

For how to *use* the frontend see
[python-verification-guide.md](python-verification-guide.md). For the
record of what already landed, use `git log` — this doc deliberately does
**not** carry the wave-by-wave changelog that earlier roadmap drafts did.

## Status legend

- **PLANNED** — concrete fix shape and scope below; ready to pick up.
- **PARTIAL** — substantially landed; specific residuals listed.
- **NO PLAN YET** — known gap, no committed approach (open question recorded).
- **BLOCKED** — needs a CBMC-core change or another section to land first.

## Working principles (apply to every item)

- **Correctness per the Python Language Reference (PLR)** is the hard
  constraint. Prefer a sound over-approximation or a documented residual
  over a precision win that risks a false proof.
- **Architectural fixes over point fixes** — when several failing tests
  share a root cause, fix the root.
- Keep all three suites green (`regression/python`,
  `regression/python-strata-tests`, `regression/python-strata-tests-pending`)
  and the ESBMC sweep at or above its baseline PASS count.
- `ulimit -v 8388608` before every CBMC invocation; commits pass
  `git-clang-format --binary clang-format-15 HEAD^`; attribute with
  `Co-authored-by: Kiro <kiro-agent@users.noreply.github.com>`; each fix
  lands with a focused regression test.

---

## 0. Soundness-direction gaps (verified false proofs) — TOP PRIORITY  {#false-proofs}

**Refreshed triage (2026-06-09, sweep PASS 2916/3091).** Of the 23
*expected-FAILED / got-SUCCESSFUL* DIFFs, **none is a genuine
high-value false proof**: 22 are flag/scope artifacts and 1 is a niche
embedded-NUL edge. Breakdown verified by hand:
- **ESBMC-only flags the cbmc frontend doesn't implement** (the test's
  bug-detection depends on them): `--strict-types` (github_3020_5,
  github_3093_1/2), `--fixedbv` (neural-net_fail), `--function`
  (ethereum_bug-fail). The sweep runs uniform `--unwind 10`, so these
  "pass" SUCCESSFUL.
- **`--incremental-bmc` tests** (15): the sweep's fixed `--unwind 10`
  can't replicate incremental bug-finding. Four of these
  (`github_2224-fail`, `github_2892_fail`, `github_3836_fail`,
  `global2_fail`) verify FAILED correctly at `--unwind 25` — pure
  under-approximation artifacts. The rest are out-of-scope by nature
  (import-error detection, `input()`, regex always-truthy `re.Match`,
  unsupported `str.encode`, missing-return opt-in, and the deferred
  `github_3647_9_fail` dict-mutation-during-iteration).
- **One genuine but niche edge:** `string-nondet-in-embedded-null-longer-fail`
  — `assume(s == "a\0b")` then `assert "a\0bc" in s` (a needle longer than
  the haystack can't be a substring). Tied to `nondet_string` length /
  embedded-`\0` substring semantics (possibly a vacuity from the length-4
  nondet vs length-3 literal). Low value; recorded with the string cluster
  in [§9](#precision).

Net: the frontend is effectively free of genuine false proofs; the only
deliberately-deferred soundness item is `github_3647_9_fail`
(dict-mutation-during-iteration, below).

### Earlier triage history (2026-06-08)

From the prior per-test triage of the 26 baseline DIFFs in the
*expected-FAILED / got-SUCCESSFUL* direction: **20 were out-of-scope**
(import-error detection, opt-in `--python-check-annotations` /
`--python-missing-return-check` not passed, the always-truthy `re.Match`
modelling choice, ESBMC-only intrinsics/flags such as `nondet_*` /
`__ESBMC_assume` / `--strict-types` / `--fixedbv`, and the known
`github_3836` recursion-under-`--unwind 10` artifact). A further two
(`github_2892_fail`, `global2_fail`) are **not** real — they verify
FAILED correctly under adequate unwinding; their sweep SUCCESSFUL was a
uniform-`--unwind 10` under-approximation artifact.

The genuine false proofs and their status (2026-06-08):

- **`github_3647_12_fail` — nested `dict.items()` value not extracted —
  FIXED (commit `97ceba8e0c`).** Root cause was *not* in `dict.items()`:
  `convert_type_annotation(dict[K,V])`'s `is_safe` allowlist omitted dict
  and set, so `dict[str, dict[str, int]]` degraded to `int`, the parameter
  carried no value, and assertions over it were vacuous. Fixed by adding
  dict/set to the allowlist (whole-group: repairs every nested-dict /
  set-valued dict annotation).
- **`github_2897_2_fail` — imported module-level constant not bound —
  FIXED (commit `4988dc5755`).** `from MODULE import name` never bound a
  module-level constant (only TypedDict was special-cased), so the name
  was undefined and `assert name == X` was vacuous. Fixed by registering
  module-level constants in `process_imported_module` and emitting an
  explicit binding ASSIGN at the import site (whole-group: every
  `from X import <constant>`; also repaired the errno/signal library test
  that had been passing vacuously).
- **`class-attributes_fail` — DEFERRED, root-caused (string-refinement
- **`class-attributes_fail` — FIXED (commit `f1ea65ad1b`;
  string-refinement temp scoping).** A genuinely-false final assert
  (`my_car.get_age(2025) == 4`, actually 3) verified SUCCESSFUL because the
  path was **vacuous**. Bisected minimal trigger: an f-string-returning
  method (`Vehicle.get_info`, `f"{self.year} {self.model}"`) invoked on two
  distinct objects in one run — directly on a base instance *and* via
  `super().get_info()` from a `Car.get_info` override.
  **Root cause:** `make_nondet_string` named the f-string's output symbols
  (`__string_len_N` / `__string_ptr_N`) globally with no function scope and
  never DECL'd them, so every dynamic invocation of the method shared one
  symbol; the refinement backend conjoined both content associations →
  UNSAT → vacuous proofs. **Fix:** inside a function, the symbols are now
  named under that function and DECL'd, so symex grants each invocation a
  fresh instance (the mechanism the `$tmp` call-return temporaries already
  use). Scope limited to the f-string emission path: scoping string
  *transforms* whose result escapes the function (`self.x = s.lower()` read
  after `__init__`, `github_2992_lower`) regresses them because a
  function-local backing dies at return; f-string results are consumed
  within evaluation or copied out by value, so scoping is safe there.
  Two earlier approaches were ruled out: unconditional havoc (regresses
  `github_2992_lower`'s object cap) and `force_havoc` (its nondet-pointer
  havoc exposes a latent `jpl`/`jpl_1` counterexample). The DECL approach
  avoids both. **Side effect:** the fix also removed the (same-mechanism)
  vacuity that had been masking `jpl`/`jpl_1`; they now surface a separate
  pre-existing precision gap — see [§9](#precision).
- **`github_3647_9_fail` — dict mutation during iteration — DEFERRED
  (niche, regression-risky).** `for k, v in d.items(): d["x"] = 3` raises
  `RuntimeError` ("dictionary changed size during iteration") in CPython;
  we don't model concurrent-modification detection. A sound model must
  (a) track the iterated dict's identity *through* `.items()`/`.keys()`/
  `.values()` and (b) distinguish size-changing mutations from
  value-updates (`for k in d: d[k] = ...` must **not** fire) — a delicate
  loop-body analysis with false-positive risk on a common pattern.
  Disproportionate to its niche value; left as a documented residual.

Net: 3 of the 4 genuine false proofs closed (`github_3647_12_fail`,
`github_2897_2_fail`, `class-attributes_fail`); only `github_3647_9_fail`
(dict mutation during iteration — niche) remains deferred. The
`class-attributes_fail` fix also removed the same-mechanism vacuity that
had been masking the `jpl`/`jpl_1` sweep entries, which now surface a
separate pre-existing precision gap (see [§9](#precision)) — i.e. three
vacuous false proofs eliminated, at the cost of exposing one sound
precision false-positive.

---

## 1. Generators / `yield` (PLR §6.2.9)  {#generators}

**Status: DONE for the modelled scope.** The **list-with-cursor** model is
implemented (see the architecture doc's "Generator semantics" section):
each `yield X` becomes `__gen_result.append(X)`, the function returns the
eager list, and a call site allocates an int cursor that `next()`
advances, raising `StopIteration` through the exception flags. This is
sound for the eager model (side-effect ordering between yields is not
faithful, which is acceptable for verification).

The two residuals that earlier drafts listed here — free-variable
resolution for module globals in generator `if`-conditions, and list-shape
propagation across function boundaries for `for x in g` — are **both
resolved** (verified 2026-06-08). The whole `github_3701` cluster verifies
SUCCESSFUL with its per-test flags (`_2/_4/_5/_9/_11/_if_else` and the
rest) and is PASS in the sweep baseline, with one exception:

- `github_3701_14` (TOERR): a recursive `f(k)` doing `ret.extend([1] + r)`
  whose `test.desc` uses ESBMC-only flags (`--smt-during-symex`,
  `--smt-symex-guard`) that this CBMC build rejects. It does not use
  `yield` — it is a recursion/ESBMC-flag artifact, not a generator gap.

**True state-machine resumption** (faithful side-effect ordering): **NO
PLAN YET** — the list-with-cursor model is the deliberate design choice;
a resumption encoding is only worth it if a benchmark needs faithful
inter-yield side effects.

---

## 2. Closures & late binding (PLR §4.2.2)  {#closures}

**Status: PLANNED (precision improvement; design only).** This is a
**sound precision gap, not an unsoundness** (verified 2026-06-08):
*non-escaping* closures are already correct — late binding within the
defining scope (`x = 10; g = lambda: x; x = 20; g()` → 20) and `nonlocal`
mutation both work (the latter via the `qualify_name` nonlocal redirect).
An *escaping* closure (returned or stored and called later) over-
approximates its captured free variables to **nondet**, so the
late-binding idiom `fns = [lambda: i for i in range(3)]` yields nondet
rather than the PLR-correct final value — a **false positive** (sound
direction), never a false proof. Tracked by `closure-late-binding-knownbug`.

The precision fix is a **cell substrate** mirroring CPython's
cell/free-variable model. Five phases:

1. **Cell-variable identification pre-pass.** Compute, per scope,
   `cell_vars(S) = assigned(S) ∩ free_vars(nested defs in S)`. Reuse the
   existing `collect_assigned_locals` + name-reference scanners.
2. **Cell storage for non-escaping closures.** Box each cell variable as
   `__cell_<name>` (a 1-field struct / pointer) and auto-dereference at
   read/write in `convert_name` and the assignment chokepoint. ~50-line
   refactor.
3. **Closure value + escaping.** Represent a closure as a record
   `{ code *fn; cell *captures[] }`; replace the value-argument capture in
   `python_converter_call_user.cpp`.
4. **Higher-order through containers.** A callable stored in a list/dict
   element needs a tagged callable element type (depends on
   [§12 higher-order functions](#higher-order)).
5. **Comprehensions with closures.** Do not unroll a comprehension at
   conversion time when its element is a closure over the iteration
   variable; route through `emit_listcomp_loop` so each iteration binds a
   fresh cell.

**Scope:** phases 1–2 are the high-value core (fixes the late-binding
class); 3–5 escalate with each higher-order use. Each phase has a PLR
correctness checkpoint.

---

## 3. Strings: native SMT-LIB String backend  {#strings}

**Status: PARTIAL.** Python `str` is modelled as CBMC's refined-string
struct (`python_string`, tag `__CPROVER_refined_string_type`), routed
through the refinement-string solver via `emit_string_function`. The
backend-selector infrastructure has landed: a `python_string_kindt`-style
selector and the `--python-smt-strings` flag exist and are threaded through
`python_language.cpp`. What is **not** done is the migration that would let
the frontend target either backend uniformly, and the SMT-String backend
implementation itself.

**Spike (2026-06-09/10, `github_3090_4`) — diagnosis corrected.** The
blocker is *not* `char*`-vs-`char[]` (JBMC's refined string is *also*
`{length, char*}` and proves fine) and *not* the `array_pool` mechanism
itself. It is **variable indirection + content storage**: `array_pool.find`
already extracts the real array (crash-free, even with symbolic elements)
when the content pointer is the syntactic form `address_of(index(<array>,
0))`, but falls through to a *fresh unconstrained* array when the pointer is
a `member` (e.g. `s.data` once the string is stored in a variable). Adding
an explicit association fixed `chr(i)=="f"` + `github_3090_4/5` under the
default backend (soundly) **but crashed `github_3130_fail`** — because
`chr` used *static, shared* content storage, so one constant pointer was
re-associated across loop unwinds. JBMC avoids this by giving each string
**per-execution heap content** (distinct pointer per iteration), so the
real bottleneck is **per-execution content storage**, not association.
**Two viable, sound routes:** (a) JBMC-style per-execution storage (fresh
allocation per producer) + the existing association — the "bigger change";
(b) **symex content-pointer dereference** — verified feasible
(`value_set_dereferencet` resolves `*(p+i)` for symbolic `i`; hook is the
already-`cprover_string`-scoped `constant_propagate_assignment_with_side_
effects`), re-materialising a bounded literal array that routes through
`find`'s crash-free fast path with **no front-end storage change** and **no
loop crash**, at the cost of bounded-deref perf + a scoped core-symex
change. Route (b) is the lower-impact lean. A throwaway **prototype
(2026-06-10, reverted) validated route (b)**: `chr(i)=="f"` and
`chr(122) not in "abc"` prove **soundly** and `github_3130_fail` is
**loop-safe (no crash)** — but a *broad* `symex_assign` hook regressed 9
sweep tests (constant strings, multibyte UTF-8 `chr`, concat results) with
0 new gains, so a production version must be **carefully scoped** (only the
symbolic/variable-indirection case; preserve constant/literal/multibyte
fast paths; materialise concat *producers*, not just comparison operands).
**Decision (2026-06-10): route (b) is adopted** as the production direction
— symex resolves content pointers to their array *object* (not per-element
unroll), the Python front-end drops `array_pool`-style association, and Java
migration follows later. **Phase 1 landed (2026-06-10):** symbolic `chr`
content equality/contains and symbolic-`chr` concat chains
(`github_3090_4/_5`) prove soundly and loop-safely, zero sweep regressions;
see the design-decision section. Implementation discipline + sequencing in the
[design-decision section](architectural/python-string-phase2-backend-abstraction.md#design-decision-2026-06-10-choice-b--symex-content-pointer-resolution).
The SMT-string backend (`--python-smt-strings` / CVC5) remains an orthogonal
precision option.
Full analysis (JBMC loop handling, `find` fast path, storage options,
symex-deref pros/cons, prototype results):
[python-string-phase2-backend-abstraction.md](architectural/python-string-phase2-backend-abstraction.md#update-2026-06-10--corrected-conclusion--symex-deref-feasibility).

**Plan (5 phases; phases 1–2 designed, 3–5 open):**

1. *Inventory* (done) — ~50 frontend sites reach into the refined-string
   struct (`build_string_struct`, `.length`, `.data[i]`, scratch-loop
   comparisons), classified as producers / consumers / mutators.
2. *Backend abstraction design* (done) — opaque string handle, a literal
   constructor intrinsic, and a per-intrinsic lowering table. The detailed
   spec (intrinsic ↔ SMT-LIB term table, `smt_string_typet`, backend impact
   on `smt2_conv` / `boolbv` / `string_refinement`, PR ordering) lives in
   [architectural/python-string-phase2-backend-abstraction.md](architectural/python-string-phase2-backend-abstraction.md)
   — keep that as the implementation spec.
3. *Frontend refactor* (open) — migrate every site off the concrete struct
   onto intrinsics: `build_string_struct` → `cprover_string_literal_func`
   (33 callers), `.length` → `cprover_string_length_func` (17),
   `.data[i]` → `cprover_string_char_at_func` (7), concat/repeat producers
   → `cprover_string_concat_func` / a new `cprover_string_repeat_func`, and
   add the 9 not-yet-available intrinsics (substring/replace/split/strip/
   find-from) with `ID_` entries + axiom handlers. One PR per group.
4. *Backend implementations* (open) — implement each intrinsic's SMT-String
   lowering in `smt2_conv.cpp` per the phase-2 table; add `smt_string_typet`
   and its `convert_type` mapping to SMT `String`.
5. *Retire hacks* (open) — remove the `smt2_conv` subject-literal
   materialisation workaround and the Python→C `str` marshalling special
   case once 3–4 land.

**Why it matters:** this unblocks precise symbolic regex (see
[§4](#regex)) and removes a class of refined-string ↔ pointer-analysis
performance cliffs (related to [§5](#dict-byref)).

---

## 4. Regex (`re` module)  {#regex}

**Status: PARTIAL.** Current support is a shallow library stub plus
`__cbmc_re_*` SMT intrinsics, and a Stage-1 call-site `regex-no-match`
check that flags statically-impossible matches. The current-state
reference is [python-frontend-regex-story.md](python-frontend-regex-story.md).
The architectural invariant: the **frontend emits refined-string
arguments; the backend bridges them to SMT `String`**.

**Already built (verified 2026-06-08):**

- The **subject → SMT-String bridge (Approach C2) is implemented** in
  `smt2_conv.cpp` (regex-intrinsic interception around the
  `cprover_string_{match,search,fullmatch}_func` lowering): a constant
  pattern is translated by `python_regex_to_smt.cpp`, a constant subject
  lowers to a precise `(str.in_re "subj" re)`, and a *symbolic* subject is
  bridged from the refined-string struct via
  `str.++ (str.from_code (bv2nat (select array i)))` truncated to length.
  Unsupported patterns / unrecognised subject shapes fall back to a sound
  `bv0`.

**The actual remaining gaps (the Wave-2 payoff), verified 2026-06-08:**

1. **Symbol subjects fall through to `bv0` (the deep gap).** The bridge's
   subject extractor only recognises a *syntactic* refined-string
   `struct_exprt{len, address_of(index(array, 0))}`. A subject that is a
   plain symbol (the common `s = nondet_str()` case) — whose bytes live in
   the string-refinement `array_pool`, not syntactically in the expr —
   hits the sound `bv0` fall-through, so the match is *never* taken and
   queries over symbolic subjects are vacuous (measured: both
   "`matches ⇒ len≥1`" and the contradictory "`matches ⇒ len==0`" verify
   SUCCESSFUL, i.e. the branch is unreachable). Closing this needs
   `smt2_conv` to expose an `array_pool`-tracked refined string to the
   SMT-LIB String theory — deep CBMC-core work (shared with JBMC code
   paths; the spec mandates a JBMC regression run per PR). Constant-subject
   matching already works precisely under `--cvc5` (`re-wave2-cvc5`).
2. **Library `Match`/`None` result not tied to the intrinsic.** The `re`
   stub calls `__cbmc_re_{match,search,fullmatch}` but always returns
   `Match()` (a deliberate choice so `re.match(...) is not None` stays
   provable under the nondet default). Even with gap 1 fixed, a flag-gated
   `--python-strict-re-result` is needed so a matched call returns `Match()`
   and a proven no-match returns `None`, without regressing the existing
   `re*` tests that rely on always-`Match()`. Smaller than gap 1, and only
   meaningful once gap 1 lands.
- **Compilation flags** (`re.IGNORECASE` etc.): currently fall back to
  nondet. *Fix shape:* rewrite the regex AST per flag before lowering.
- **Wave 3 — native regex axioms in the string-refinement loop: NO PLAN
  YET** (research-grade; deferred). Back-references, lookahead, and capture
  groups are explicitly out of scope.

---

## 5. dict pass-by-reference & value-string storage  {#dict-byref}

**Status: DONE.** List and class-instance parameters pass by reference
soundly. Dict parameters now do too, for **all** key types:

- **Option B** (commit `714ca9866b`) landed by-reference for key-matching
  (string-keyed) dicts via the shared `safe_typecast` container promotion.
- **Option A** (commit `9da530b0e4`) then inlined the refined string into
  `python_value` (`__str` field instead of `__str_ptr`), which removed the
  string-refinement perf cliff and unblocked flipping the bare-`dict`
  default to `dict[value, value]`. With that, a **non-string-keyed** dict
  argument also promotes through the generic boundary (keys and values
  both widen to the tagged union) and its mutations propagate — closing
  the last dict pass-by-reference latent unsoundness.

The full diagnosis and option analysis that led here is preserved in
[python-frontend-dict-byref-plan.md](python-frontend-dict-byref-plan.md).
Remaining (precision, not soundness): a dict literal larger than
`PYTHON_MAX_DICT_SIZE` is bounded; deeply heterogeneous value-keyed dicts
carry the usual tagged-union precision cost.

---

## 6. Module / library support  {#modules}

**Status: PARTIAL.** Import resolution, library models (`random`,
`datetime`, `math`, …), the `@c_intrinsic` decorator route, and the
stub-loading pipeline are landed (see the architecture doc). Library
files live in `src/python/library/<name>.py` and are ingested like user
code.

**Open:**

- Migrate ad-hoc `math.*` handling to a declarative `library/math.py`;
  model more C-backed modules (`cmath`, `os.getenv`, `time.time`); provide
  `nondet_string` helpers; document stub-authoring conventions.
- Model `datetime`, `dataclasses`, `collections`, `json` more fully;
  migrate third-party stubs from the benchmarks repo into this repo.

*Fix shape:* each is a stub-authoring task (pure-Python model + optional
`@c_intrinsic` for C-backed primitives), low architectural risk.

---

## 7. `--python-check-annotations` default-on blockers  {#check-annotations}

**Status: BLOCKED** on two CBMC-core issues; the flag stays off-by-default.
The annotation checker itself is implemented and correct when it runs.

- **Issue 1 — `boolbv_map` width mismatch** (`boolbv_map.cpp:68`
  invariant). A symbol re-created with a different width trips the map's
  consistency invariant. *Fix direction:* either type-check on lookup, or
  force consistent width at symbol creation in the frontend.
- **Issue 2 — solver ERROR per annotation property** (seen on
  `websocket_url_validator`). *Frontend-side mitigation:* short-circuit the
  annotation-mismatch check when the declared element type is
  `python_value` (Any), avoiding the construct that trips the solver.

---

## 8. Performance optimization backlog  {#performance}

**Status: PARTIAL.** Landed: the `irept::compare` sharing fast-path,
`--slice-formula` default-on (the slicer ↔ string-refinement contract is
documented in
[architectural/python-perf-analysis.md](architectural/python-perf-analysis.md)),
and the **parse daemon** (Unix-socket `python_ast_server.py`, eliminating
per-module Python startup). The empirical 51-benchmark analysis (layered
call-stack breakdowns, per-outlier trajectory) is the reference in that
doc.

**Open optimization targets** (from the analysis):

- **`python_value` SSA expansion.** Field-by-field SSA on tagged-union
  structs dominates some benchmarks. *Fix shape:* cap `field_sensitivity`
  recursion depth or emit struct-level SSA for tagged-union assignments.
- **`--python-required-kwarg-checks` axiom volume.** *Fix shape:* an O(K)
  hash-based formulation replacing the current O(K²).
- **`irept::operator==` in `merge_irept::merged`.** *Fix shape:* pre-cache
  `number_of_non_comments`, or switch the relevant maps to `unordered_map`.
- **Solver-side tuning** for solver-bound outliers (cvc5 theory hints).

**Parse-daemon follow-ups:** a CMake `FIXTURES_SETUP/CLEANUP` so
`ctest -R python-` auto-starts the daemon; an in-memory parse cache keyed
on `(path, mtime)`; a multi-process pool for parallel parse requests.

---

## 9. Precision clusters (sound today; precision misses)  {#precision}

All items here are **sound** (misses / over-approximations, never false
alarms). Verified against the 2026-06-08 sweep baseline.

- **String operations:** the `string-concat` loop cluster
  (`string-concat4/5/6/13`) and `string.digits` / `string.ascii_uppercase`
  population are **all PASS now** — closed. No open items in this group.
- **ESBMC-nondet primitives (PARTIAL):** most of the previously-listed
  residuals are **closed** (`nondet_list17/18`, `nondet_dict14` now PASS).
  Still open (DIFF): `nondet_list4` (a typed-int nondet element can take
  the None sentinel; excluding it would be an under-approximation) and
  `nondet_list5` (loop-unwinding sensitivity).
- **TIMEOUT tests (PLANNED per-test, 12 open):** as of the
  2026-06-08 baseline (`PASS 2905`): `dict65`, `github_3560_1`,
  `github_3560_3`, `github_3560_4`, `github_3626-nondet`,
  `github_3667_2-nondet`, `github_3684`, `list31`, `nondet_dict13_fail`,
  `nondet_list6`, `redundancy`, `shedskin`. (The inline-string change did
  not move these — they are not string-refinement bound.) No shared root
  cause — each needs a profile to find the hot path (dict `.items()`
  schema-walking, type-promotion multiplication, symbolic-size ×
  bounded-unroll interactions).
- **`complex` precision (PARTIAL — numeric core fixed 2026-06-08).** A
  fresh triage of the 10 `complex_*` DIFFs found the cluster is *not* one
  root but six sub-groups. The two genuine **numeric-precision** roots are
  fixed: (a) `abs(complex)` now pins the IEEE boundaries (zero/inf/nan)
  instead of leaving a non-injective `r*r == re²+im²` constraint
  (`94e5d2fef4`); (b) integer/bool complex powers now use exact repeated
  multiplication, exact and symbolic-base-capable, instead of the lossy
  `exp(w·log z)` form (`c0e69a8aca`). Gained complex_abs_handler,
  complex_pow_handler, complex_attr_div_pow, complex_pow_special_cases
  (sweep PASS → 2911, no regressions). **(E) `complex()` argument-validation
  TypeErrors** — unknown kwargs, `real`/`imag` duplicating a positional
  arg, and `bytes`/`bytearray` rejection — is also **fixed** (`f4afdb0ac3`,
  +complex_keyword_args). **(F) `math.*` on a complex argument →
  TypeError** is **already handled architecturally**: the `math.X` dispatch
  rejects complex for every real-domain function (except `prod`/`sumprod`)
  and recursively walks List/Tuple literals and names bound to complex/list
  literals — verified for direct positional args, kwargs from a literal
  dict, and list-literal args. The remaining sub-groups are *not* numeric
  precision and remain open as distinct point/feature gaps:
  **(C) signed-zero preservation** through `complex()` construction and
  +/−/* (e.g. `complex(-0.0,0.0).real` must keep its sign) — delicate IEEE,
  low value; **(D) `complex(<str variable>)`** parsing — only constant
  strings parse today, a runtime form needs solver-level string parsing;
  **(F-residual) indirect complex-argument detection** —
  `complex_math_typeerror_edges` stresses complex reaching `math.X` via
  `**kwargs` from a *function-returned* or *aliased* dict, via an
  unannotated function return, inside a `sumprod` list arg, and the
  exception-ordering case (`ValueError` from `complex("bad")` before the
  complex-guard `TypeError`). These are fragile indirection point-cases
  with no shared root; left as a documented residual. (C)/(D)/(F-residual)
  still affect complex_binop_promotion, complex_conjugate_handler,
  complex_builtins, complex_constructor_extended,
  complex_math_typeerror_edges.
- **`math` precision (PARTIAL):** per-function domain handling. *Fix
  shape:* declarative `@c_intrinsic` domain annotations (depends on
  [§6](#modules)); cross-function tracking of return constants for
  dict/list literals.
- **`jpl` / `jpl_1` — FIXED (2026-06-08/09).** Both had reached
  `counter == -1` (which CPython never does) after the string-scoping fix
  removed the vacuity masking them: a state machine filters enabled
  actions with `list_comp(actions, lambda a: a.pre())` then runs
  `enabled_actions[random.randint(0, len-1)].act()`. Four layers were in
  play and all are now fixed: (a) the lambda's object parameter typed as
  nondet int — `4d808cefda`; (b) `list_comp` calling its function-valued
  parameter — per-call-site monomorphisation `54b829c6bd`; (c) the
  monomorphised clone comprehending over its list *parameter* —
  specialising the clone to call-site argument types + re-inferring its
  return type `eb80586ebc`; (d) the loop-based variant building the
  enabled list via `result.append(candidate)` and dispatching
  `enabled[idx].act()` — preserving the object element type on append
  `f771788f7c`. Both now verify SUCCESSFUL **soundly** (the `counter`
  invariant holds; `counter == -1` is unreachable, as in CPython).
- **`github` real-world cluster — refreshed triage 2026-06-09 (17
  precision DIFFs).** Grouped into shared-root sub-clusters (biggest /
  cleanest first):
  - **Enum (2) — FIXED (`4f58e3f7d5`).** `github_3642` (member `==` /
    `light == TrafficLight.GREEN`), `github_3642_alias` (`A.X.value` with
    `Enum as E` base). Member `.value`/`.name` resolution + enum-typed
    parameters + aliased-base recognition; see [§12](#higher-order)-style
    pre-scan in convert(). PASS 2916 → 2918.
  - **Heterogeneous dict values (3) — investigated; no sound fix.** The
    mixed-value modelling is already correct: `convert_dict` promotes a
    heterogeneous value array to `python_value`, and tagged comparison
    works (verified). `github_3719_4/5-nondet` fail only because
    `nondet_float()` may be **NaN** and `v == NaN` is correctly False (the
    assertion is false for NaN in CPython too); excluding NaN to "pass"
    would be **unsound**. `github_3783_5-nondet` is a different issue —
    `popitem()` after a string-keyed `d["y"]=v` returns the wrong key,
    which reduces to **symbolic-string-key equality** (int keys work; see
    below).
  - **`chr()` string building (2) — investigated; string-refinement.**
    `github_3090_4/5` (`s += chr(i)` over `nondet_int` assumed-constant
    code points, then `assert s == "foo"`). Constant `chr()` already folds
    and compares correctly; the symbolic case builds a non-interned string
    struct, and string `==` compares structurally (length + pointer)
    rather than by **content**, so it never equals the interned literal.
    This same **symbolic-string content-equality** gap underlies the
    `github_3783_5` string-key `popitem`. It is string-refinement
    territory best addressed by the native SMT-LIB String backend
    ([§3](#strings)), not a fragile point fix.
  - **Module-stub + isinstance (2):** `github_2960`, `github_3286`
    (`import ll; ll.create(...)` then `isinstance(x, ll.Bar)`).
  - **Container-stored / default-arg callables (2) — FIXED
    (`efb707770a`):** `github_3690` (`{'+': lambda: 1.0}[x]()` — a callable
    in a dict-literal callee) via dict-of-callables dispatch + KeyError;
    `github_3707` (`g = f; def h(op=g): op(...)` — function as default arg)
    via default-arg callable monomorphisation. See
    [§12](#higher-order). Bonus gain `lambda12`.
  - **Singletons (per-test root, triaged 2026-06-09):**
    - `github_3772_3` (annotation says `str`, returns `int`) — **FIXED
      (`afe6acb0e0`)**: a variable annotation is a hint, so an
      incompatible scalar/string RHS keeps its value instead of coercing
      to nondet.
    - `github_3313` (isinstance-narrowing on `str | datetime`) — **FIXED
      (`49fcc4a5fe`)**: a class instance assigned to a tagged-union
      variable is now constructed into a temp and wrapped (CLASS tag +
      `__class_ptr`); previously `__init__` ran on a nondet self so the
      instance was uninitialised and the post-narrowing field read was
      nondet.
    - `github_3728` (`y.tail.head` over `Optional["List"]`) — **FIXED
      (`be02fbcffa`)**: `Optional[ClassName]` now lowers to the tagged
      union (like `Optional[container]`/`Optional[scalar]`), so a
      self-referential field reaches the instance via `__class_ptr` and is
      fixed-size, instead of being truncated to a `{__class_tag}`
      placeholder. Linked lists and trees verify; a None link still fails
      soundly (AttributeError). This + 3313 are the same architectural
      capability — the **class-instance ↔ python_value union boundary**
      (construction wrapping + field access through `__class_ptr`).
    - `github_2960` / `github_3286` (cross-module class resolution) —
      **FIXED (`15961b484f`)**: (a) `process_imported_module` resolves the
      imported module's own imports transitively (so ll.py's
      `from md import Foo` registers md's classes), (b) `isinstance` now
      resolves a module-qualified classinfo (`ll.Bar`), (c) `@overload`
      stub defs are skipped in imported modules so the real implementation
      registers.
    - `github_3667` (shallow `list.copy()` inner-list aliasing) —
      **substantial (open):** `list.copy()` is a struct (deep) copy, but
      Python's copy is shallow (inner lists shared), so after
      `nested[0].append(99)` the snapshot `shallow[0]` is still length 1
      and `shallow[0][1]` raises a spurious IndexError (imprecision, not
      unsoundness). The inner lists in `[[1],[2]]` are *anonymous* nested
      literals stored by value; `escaped_mutables` only makes *named*
      lists by-reference, so it does not apply. A sound fix needs
      anonymous nested mutable literals stored by reference + shallow-copy
      sharing + subscript/append deref — the full nested-container
      by-reference root; no minimal sound fix exists on the value-based
      representation.
    - `github_3560` (`input()` + `split`), `github_3594` (`"ß".upper()`
      unicode case mapping) — reduce to the symbolic-string /
      string-refinement root ([§3](#strings)).
- **`decimal` cluster (4) — P1/P2/P3 LANDED (`8f8ebf3aea`, `2fcd9c7b46`;
  see [decimal plan](python-frontend-decimal-plan.md)):** the old
  `decimal.py` stub modelled `Decimal` as a **float wrapper**, unsound for
  exact decimal (e.g. `Decimal("0.1") + Decimal("0.2") == Decimal("0.3")`
  is True for `Decimal` but False in float) and unable to construct from a
  string. Replaced with a base-10 exact `(sign, coefficient, exponent)`
  model: a converter intrinsic parses `Decimal(<str/int literal>)` into
  the typed struct, and the stub implements comparison/arithmetic on the
  parts, aligning exponents via an integer `_pow10` loop (NOT `10 ** n`,
  which Python types as float). P1 (parse + accessors + equality/ordering)
  + P2 (`+,-,neg,abs,*,//,%`) cover all four `decimal*` tests. P3 added
  float/symbolic construction soundness (now an unconstrained finite
  Decimal, not 0 — which had let `Decimal(1.1) == 0` false-prove),
  special values (Inf/NaN), `quantize` (ROUND_HALF_EVEN), and exact
  terminating `truediv`. **Sound residuals:** non-terminating division +
  `sqrt` (28-digit context rounding exceeds 64-bit → nondet),
  Inf-comparison precision (nondet), bounded exponent alignment.
  Separately, `!=` now dispatches to `__ne__`/negated `__eq__` for any
  class with custom equality (`7367a713bf`), fixing value-based `!=`.
- **`lambda7` / `lambda18` body emission:** **closed** (both PASS in the
  2026-06-08 baseline).

> **Re-validation note (2026-06-08):** this doc was consolidated from a
> wave-41-era roadmap, and several entries proved stale on inspection —
> the generator cluster (§1), the regex bridge state (§4), the TIMEOUT
> list, and most of the string/nondet/lambda items above were already
> resolved. The **actual** 2026-06-08 sweep baseline (PASS 2905/3091) has
> 97 genuine DIFF mismatches — far fewer than the old roadmap implied —
> bucketed as: ~30 `github_*`, ~10 `complex_*`, ~3 `nondet_*`, and ~54
> other (notably a `casting*-fail` group). The `math`/`complex` and
> `github` fix-shapes above are still directionally right, but their
> scope is smaller than stated; a per-test DIFF triage should precede any
> push on these clusters.

---

## 10. Attribute / descriptor protocol residuals (PLR §3.3.2)  {#descriptors}

**Status: PARTIAL.** `@property` on all receiver shapes (incl. inherited
via MRO), `__getattr__` fallback, and stateless custom-descriptor `__get__`
are landed.

**Residuals (KNOWNBUG, sound):**

- **Non-data-descriptor (method) shadowing** (`method-shadow-knownbug`):
  an instance attribute shadowing a method.
- **Custom-descriptor `__set__` + stateful `__get__`.** *Fix shape:* both
  need an instance-`__dict__`-as-storage model (class-object construction),
  which is a larger substrate than the current per-field struct.

**NO PLAN YET** for the instance-`__dict__` substrate; it would also
subsume dynamic attribute assignment.

---

## 11. icontract → DFCC contracts  {#icontract}

**Status: PARTIAL (substantially landed).** `@require` / `@ensure` /
`@snapshot` / `@invariant` route into CBMC's DFCC machinery; single-level
Liskov inheritance composition (precondition OR-weakening, postcondition
AND-strengthening) works, with a regression suite. See the architecture
doc's contracts section for the bridge semantics.

**Residuals:**

- **Multi-level Liskov inheritance** composition across a 3+-deep class
  chain — single-level is confirmed; multi-level is not. *Fix shape:*
  recurse the OR/AND composition through the full MRO.
- **MRO precedence for mixin override conflicts** uses first-found-wins
  rather than strict C3. *Fix shape:* use the existing `class_mro` C3 order
  in the contract-inheritance walk.
- **Contracts on async functions: NO PLAN YET** (depends on
  [§13](#async)).

---

## 12. Higher-order functions / function values  {#higher-order}

**Status: PARTIAL.** Function aliasing (`g = h`), lambda-returning
functions, and bound-method reassignment are tracked via
`function_aliases` / `lambda_returning_functions` side-tables.

**Object-accessing lambda parameters — FIXED (`4d808cefda`).** A lambda
parameter used as an object (`lambda p: p.age`, `lambda a: a.m()`) was
typed by the body heuristic as `int`, so member access / virtual dispatch
on it was nondet. It is now typed `python_value` (matching regular
unannotated parameters), so these resolve on the argument's runtime class.
This is what made the polymorphic-dispatch half of jpl tractable, and it
also makes object key/predicate lambdas work as arguments to the builtin
HOFs `filter()` and `map()` (verified).

**Calling a function value indirectly — LANDED via per-call-site
monomorphisation (`54b829c6bd`).** A callable passed as an argument and
invoked through the *parameter* (`def apply(fn, x): return fn(x)`) used to
hit "no body for callee" → nondet. `convert_user_call` now detects a
positional argument that is a resolvable callable (lambda, or a name bound
to a function/lambda) whose matching parameter is *called* in the callee
body, builds/reuses a freshly-scoped **clone** of the callee with that
parameter bound to the callable (reusing the decorator `function_aliases`
+ body-re-conversion machinery), and redirects the call to the clone;
redundant callable args become nondet placeholders. The clone is cached
keyed by callee + bound-callable ids, so a loop reuses one clone and
distinct callables get distinct clones — sound for multi-callable HOFs
(no last-binding-wins). Handles lambda/named-function args and nested HOF
application; gained `higher-order2`, `callable4`, `github_3720`. The clone
is also **specialised to the call-site argument types** (refining an
unannotated `python_value` parameter to the concrete `list[int]` passed)
and **re-infers its return type from the specialised body**, so a HOF that
comprehends over its list parameter (`def keep(xs, p): return [x for x in
xs if p(x)]`) verifies precisely for both int and object elements; this
fixed **jpl** (now PASS, soundly). Falls back to the sound nondet path for
the unresolved cases below.

**Object lists built via `append` — FIXED (`f771788f7c`).** A HOF (or any
code) that builds a list of objects with `out = []; out.append(elem)` and
later dispatches `out[i].method()` used to mis-dispatch: the empty list
defaulted to a scalar element type that dropped the class tag. The
empty-list element-type prescan now infers `python_value` for object
appends (constructor, subscript `xs[i]`, or a name bound to one), and the
monomorphised clone re-runs the prescan in its own scope, so this works
through a HOF over a list parameter too. This closed **jpl_1** — the last
jpl residual — so both jpl and jpl_1 now verify SUCCESSFUL **soundly**. A
follow-up (`1ad49a3c60`) extended the subscript handler to deref a
**by-reference list parameter**, so a *non-HOF* function building an
object list from a list parameter (`def collect(xs): out=[];
out.append(xs[i])`) dispatches correctly too.

**Immediately-applied lambdas — FIXED (`528355daab`).**
`(lambda ...: ...)(args)` is now converted and called (arithmetic,
multiple parameters, and attribute/method dispatch on an object argument),
instead of falling through the empty-callee path to nondet.

**`sorted(key=lambda o: o.attr)` over objects — FIXED (`ef4e9735e7`).** The
runtime sort now compares the named attribute of each element (a member
access on the element struct) instead of the whole struct, so object lists
order correctly (ascending and `reverse=True`). Keyless sorts and the
constant-fold int/string fast paths are unchanged.

**Container-stored callables — LANDED (`efb707770a`).** A call whose callee
is a subscript of a dict *literal* of callables
(`{'+': add, '-': sub}[op](5, 3)`, `{'+': lambda: 1.0}[x]()`) now
dispatches: a result temp is assigned per entry under a `key == entry_key`
guard that calls that entry's callable, and a key matching no entry raises
KeyError (PLR §6.10). Falls through to the generic nondet callee path when
the dict values are not all resolvable callables (so non-callable dict
subscripts are unaffected). Gained `github_3690` (+ bonus `lambda12`).

**Default-arg callables — LANDED (`efb707770a`).** `try_monomorphise_call`
now also binds a parameter whose *default* value is a resolvable callable
and which is called in the body (`g = f; def h(op=g): return op(1, 1)`
called as `h()`), not only positionally-passed callables. Gained
`github_3707`.

**Residuals (sound; still open).**
- Callables stored in a *named* container (`d = {...}; d[k]()`) or in a
  list, and closures through containers
  ([§2 phase 4](#closures)), remain unmodelled — the dispatch above
  covers dict *literal* callees and the monomorphisation clone covers
  *argument-passed* / *default-arg* callables.

---

## 13. Async / await  {#async}

**Status: NO PLAN YET.** `async def` / `await` / async generators are not
modelled. A plausible shape is to reuse the generator list-with-cursor
lowering for async generators and treat `await` as a synchronous call for
verification purposes, but this has not been designed or validated against
PLR §8.8.x semantics.

---

## 14. Deferred residuals index (sound; low priority)  {#residuals}

Known sound gaps with no committed plan, kept here so they are not
rediscovered as "new":

- **dict comprehension over a runtime iterable** (`dictcomp-runtime-knownbug`)
  — currently sound nondet; needs find-or-insert + dedup store in the loop
  (the list/set comprehension loop lowering already exists; dict needs the
  keyed-store variant).
- **Comprehension scope isolation** — a comprehension's iteration variable
  can leak to the enclosing namespace (not a soundness issue).
- **Generator-expression inner-iterator variable shadow**
  (`for x in xs for x in range(x)`) — punts to the single-generator path;
  needs proper generator scoping.
- **Typed-numeric `is None` = False fast-path** (P0.F residual) — blocked
  by two patterns (annotated fall-through return widening; inherited-method
  sentinel patterns). *Fix shape:* a return-type pre-pass or per-call-site
  "implicit-None-capable" tracking.
- **`type-inference-for-len`** — symbolic-string for-loops need per-call-site
  specialisation or a bounded default unwind.

---

## Tracking conventions

When picking up an item: update its **Status** line (add the in-progress
commit ref), land a focused regression test, re-run the three suites and
the ESBMC sweep, and on completion either mark the residual closed or move
the item out of this doc (history stays in git). If an item splits, add the
refined sub-items with their own fix shapes.
