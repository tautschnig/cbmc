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

## 1. Generators / `yield` (PLR §6.2.9)  {#generators}

**Status: PARTIAL.** The **list-with-cursor** model is implemented (see the
architecture doc's "Generator semantics" section): each `yield X` becomes
`__gen_result.append(X)`, the function returns the eager list, and a call
site allocates an int cursor that `next()` advances, raising
`StopIteration` through the exception flags. This is sound for the eager
model (side-effect ordering between yields is not faithful, which is
acceptable for verification).

**Residuals:**

- **Free-variable resolution for module globals in generator
  if-conditions.** A generator whose `if flag:` reads a module-global
  `flag` drops the whole if/else body. Not generator-specific —
  reproduces with any function reading a module global in a condition.
  *Fix shape:* during the body pre-scan, track free variables read by a
  function body and ensure they resolve to the module-global symbol's
  value (or nondet) at call time.
- **List-shape propagation across function boundaries** for `for x in g`
  over a generator instance whose body indexes bound symbols
  (`rand[0]`, `len(l1)`): index-out-of-bounds on indirect list reads.
  *Fix shape:* for `for x in g` over a known generator instance, iterate
  `g.data` up to `g.length` instead of the symbol's declared
  `PYTHON_MAX_LIST_LENGTH` bound; extend the return-list-literal shape
  tracking to bound-symbol shapes.

**Scope:** ~2–3 days for both, which closes the remaining generator
cluster (`github_3701_2/4/5/9/11/if_else`).

**True state-machine resumption** (faithful side-effect ordering): **NO
PLAN YET** — the list-with-cursor model is the deliberate design choice;
a resumption encoding is only worth it if a benchmark needs faithful
inter-yield side effects.

---

## 2. Closures & late binding (PLR §4.2.2)  {#closures}

**Status: PLANNED (design only, not implemented).** Closures currently
capture by **value** at definition time, so the late-binding idiom
`fns = [lambda: i for i in range(3)]` (all should return 2) is wrong, and
a closure that mutates a captured variable does not share storage with the
enclosing scope. Tracked by `closure-late-binding-knownbug`.

The sound fix is a **cell substrate** mirroring CPython's cell/free-variable
model. Five phases:

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
reference is [python-frontend-regex-story.md](python-frontend-regex-story.md)
(what's modelled, what works, the backend-portability matrix, what
doesn't). The architectural invariant: the **frontend emits refined-string
arguments; the backend bridges them to SMT `String`**.

**Open work:**

- **Wave 2 — subject → SMT-String bridge (PLANNED).** Teach `smt2_conv` to
  bridge a refined-string subject to an SMT `String` so
  `(str.in_re subject <regex>)` fires for symbolic subjects. The contained
  form ("Approach C2") intercepts only the regex intrinsics in
  `smt2_conv.cpp` (~250 lines) rather than migrating all strings — it is
  the same dependency as [§3 phase 4](#strings) but scoped to regex.
  Closes ~11 regex precision tests incl. `sagemaker_labeling_job`.
- **`--python-strict-re-result` flag (PLANNED, ~30 lines).** A gated
  library mode where `Pattern.search` returns `None` on a proven
  no-match, for callers that branch on the result.
- **Compilation flags** (`re.IGNORECASE` etc.): currently fall back to
  nondet. *Fix shape:* rewrite the regex AST per flag before lowering.
- **Wave 3 — native regex axioms in the string-refinement loop: NO PLAN
  YET** (research-grade; deferred). Back-references, lookahead, and capture
  groups are explicitly out of scope.

---

## 5. dict pass-by-reference & value-string storage  {#dict-byref}

**Status: PARTIAL / BLOCKED.** List and class-instance parameters pass by
reference soundly; **dict parameters pass by value**, silently dropping
mutations made through the parameter (a latent unsoundness). The
value-keyed read/membership machinery (`value_equal`) is sound and landed,
but the natural enabler — a uniform `dict[value, value]` default — is
**blocked on performance** at the representation layer (value-keyed
*string* dicts explode the string-refinement solver because `python_value`
stores strings behind a pointer).

The full diagnosis, options, and recommendation are in
[python-frontend-dict-byref-plan.md](python-frontend-dict-byref-plan.md).
Summary of the path:

- **Recommended near-term (Option B):** targeted by-reference for
  *string-keyed* dicts — promote only the values array, keep keys inline;
  no representation change; sound for the common case. Needs
  `is_python_dict_type` to resolve `struct_tag` at the wrap site.
- **Root-cause (Option A):** inline the refined string into `python_value`
  — large blast radius; must be **spiked behind a measurement** first
  (the refined string still carries a `data` pointer, so the win is
  unproven). Would also help [§3](#strings) workloads.

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
alarms). Grouped by root area with rough test counts from the last DIFF
sweep.

- **String operations (PLANNED, ~3 tests open):** `string-concat` in a
  loop — `word[i]` reads on a `cprover_string_concat` result are opaque;
  `string.digits` / `string.ascii_uppercase` constants not populated from
  imports. *Fix shape:* resolve `word[i]` on concat results at the
  cprover-string layer; populate the `string` module constants in library
  hooks.
- **ESBMC-nondet primitives (PARTIAL):** residuals `nondet_list4`
  (typed-int nondet can take the None sentinel — excluding it is an
  under-approximation), `nondet_list17/18` (append-then-index of strings),
  `nondet_list5` (loop-unwinding sensitivity), `nondet_dict14`
  (`k in x` membership on nondet-content string keys — needs richer
  string-solver modelling).
- **TIMEOUT tests (PLANNED per-test, 8 open):** `dict65`, `github_3626`,
  `github_3667_2`, `github_3684`, `list31`, `nondet_list6`, `shedskin`,
  `string-nondet-in-success`. No shared root cause — each needs a profile
  to find the hot path (dict `.items()` schema-walking, type-promotion
  multiplication, symbolic-size × bounded-unroll interactions).
- **`math` / `complex` precision (PARTIAL, ~38 / ~36 tests):** per-function
  domain handling and complex arithmetic modelling. *Fix shape:* declarative
  `@c_intrinsic` domain annotations (depends on [§6](#modules)); model
  complex arithmetic edges (signed-zero, NaN, ValueError for malformed
  `complex("bad")`); cross-function tracking of return constants for
  dict/list literals containing complex.
- **`github` real-world cluster (~129 open):** many small sub-clusters
  (int(string, base) edge cases, isinstance-narrowing for union params +
  datetime stub fields, reversed-range iteration, list index-out-of-range,
  fail-shape soundness tests). Tractable but no shared root cause — triage
  per sub-cluster.
- **`lambda7` / `lambda18` body emission (PLANNED, ~1 day):** lambda body
  conversion leaks pending checks into the caller's GOTO for some concat
  shapes. Worked around for the struct-call dedup case; the underlying
  emission bug remains.

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
`function_aliases` / `lambda_returning_functions` side-tables. What is
missing is a first-class **function value** that can be stored in a
container, passed generically, and called indirectly (true function
pointers).

*Fix shape:* a tagged callable handle (code-symbol id + captured cells)
usable as a list/dict element and a `python_value` variant; dispatch at
indirect call sites. This is the shared dependency for
[§2 phase 4 (closures through containers)](#closures). **No committed
scope** — escalates with demand.

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
