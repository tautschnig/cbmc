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

## Prioritized worklist (2026-06-12)  {#worklist}

Cross-cutting list consolidating the native SMT-String work and its unblocked
follow-ons. **Both back-ends are first-class.** The refined (string-refinement /
default, no-external-solver) back-end is to *reach parity* with the SMT-String
path — the refined-precision items below are goals to pursue, **not** "use
`--python-smt-strings` instead". Items are sequenced by soundness-first, then
robustness, then capability; difficulty is noted where high.

> **Refreshed priorities (2026-06-18) — supersedes the ordering below.**
> Since the 2026-06-16 block, **closures (§2) are largely closed**: the cell
> substrate (read-only escaping capture; `nonlocal`-mutating per-invocation
> heap cells, multi-call sound; per-closure-variable binding), higher-order
> dispatch through container subscripts (`fns[i]()`, `d[k]()`), comprehension
> late-binding, and the **fat-closure** for capture-through-param + n-ary
> (multi-call sound) all landed. Remaining closure channels (container/
> attribute-stored, compose) are ~0 corpus value, sound-nondet, documented in
> [doc/python-frontend-fat-closure-plan.md](python-frontend-fat-closure-plan.md).
> A **P0 soundness audit (2026-06-18, `120fd5f4a2`)** of the nested-aliasing
> guard found and fixed two more false-proof channels (self-append,
> new-container literal); one residual (extraction from an untainted literal /
> matrix-row pattern) is documented as byref-blocked + over-report-risky.
>
> **Updated order (perf-only work deferred per direction):**
> 1. **P0 soundness** — nested-aliasing audit ✅ DONE. (P0 otherwise empty.)
> 2. **Instance-`__dict__` substrate phase 2+** ([§10](#descriptors)) — method
>    shadowing, stateful descriptors, `setattr` (phase 1 / dynamic-attr
>    discovery landed `9f52de49ee`). Contained precision substrate.
> 3. **Native SMT-LIB String backend** ([§3](#strings)) — the strategic
>    precision target; the refined-string frontier is measured as largely
>    tapped, so this is the comprehensive answer (also unblocks the
>    `github_3090` per-execution-content spike + JBMC native `smt_string`).
>    Sustained, phased effort; refined parity is the constraint.
> 4. **Breadth / capability** — module breadth ([§6](#modules), by corpus
>    import frequency); async result-binding ([§13](#async), small live bug);
>    icontract multi-level Liskov + C3 MRO ([§11](#icontract)).
> 5. **Deferred** — `python_value` field-by-field SSA (perf-only, [§8](#performance));
>    closure Phases 4–6 (~0 corpus); regex finishers ([§4](#regex), fold into
>    the native backend); `--python-check-annotations` default-on (BLOCKED on
>    core); point precision (`complex` C/D, `math` domains, `nondet_list4/5`).

> **Sweep soundness audit (2026-06-18).** Scanned the ESBMC sweep for the
> false-proof direction (we report `SUCCESSFUL` where the expected is
> `FAILED`): **21 DIFFs**, triaged against **CPython** semantics (the PLR
> soundness bar) — not ESBMC's stricter expected verdicts:
> - **10 are NOT soundness bugs** — we correctly match CPython; ESBMC is
>   stricter: annotation enforcement (`github_3020_5`, `github_3093_1/2`
>   [`mod.foo` *is* defined, called with the wrong arg type],
>   `infer-func-no-return_fail`), a list-equality depth limit
>   (`list-depth-exceed`), missing-return→`None` (`missing-return13_fail`,
>   needs `--python-missing-return-check`), unsupported-but-unused
>   `s.encode()` (`github_2993_2_fail`), and an *uncalled* function
>   (`ethereum_bug-fail`).
> - **1 BMC-bound** (`github_2224-fail`: the bug is 11 loop iterations deep,
>   beyond `--unwind 10`; chained `0<=x<=100` is correct in isolation).
> - **1 vacuously-sound** (`string-nondet-in-embedded-null`, already documented).
> - **The genuine false proofs** share a single root: ops the frontend
>   cannot decide (operations that may raise; unresolved/non-imported
>   references) were modeled as **silently succeeding nondet** — a
>   deliberate precision-favoring choice (`python_language.cpp` "uncaught
>   exception check removed — too many false positives"). Fixes landed:
>   - **Root A — `--python-raising-ops-check` (opt-in, default OFF;
>     `6cf50c9faf`, `8c5a702eaf`).** Unified `emit_may_raise` mechanism + a
>     declarative `@may_raise('Exc')` library decorator. Under the flag:
>     `int(<non-const str>)`→`ValueError` (`input1_fail`, `input5`), `os.*`
>     file ops→`OSError` (`import-os2_fail`), `re` non-str pattern→`TypeError`
>     (`re7_fail`) are modeled as may-raise. Default-off so the sweep is
>     unaffected (neutral by construction); soundness is *available* opt-in.
>   - **Root B — per-module import scoping (default-on; `e30dd51b44`).** A
>     bare reference to a name that exists only because a module was imported
>     but was not itself imported now raises `NameError`
>     (`import-from-function-fail`, `import-from-multiple-fail`). Gated to
>     main-module code (`current_function`) so imported-module bodies'
>     internal calls aren't flagged. Sweep PASS 2945→2947 (+2), 0 regressions.
> - **Re-triage of the "remaining" residuals (2026-06-18, deeper root-cause).**
>   At the sweep's `--unwind 10` three of these reported `SUCCESSFUL`, but at
>   `--unwind 25` they correctly `FAILED` — so they are **BMC-unwind-bound
>   artifacts, not frontend false proofs** (like `github_2224-fail`): the bug
>   lies beyond the configured bound (`github_2892_fail` iterates a 28-char
>   string; `global2_fail` has `range(15)`; `github_3836_fail` recurses).
>   List-OOB *is* bounds-checked in isolation. These need no frontend change.
>   - The **two genuine frontend false proofs** that remain at any unwind:
>     - **`re10_fail` — FIXED (`a3976b72df`).** The real root was narrower than
>       "type inference": `convert_unary_op` unwrapped *every* `not` operand to
>       `int` via `unwrap_value(_, python_int_type())`, so a tagged-union
>       (`python_value`) holding a CLASS/STR/LIST/DICT instance was tested on
>       its `__int_val` slot — making any present object (e.g. a successful
>       `re.match`'s `Match | None`) falsy. Fix: `not x` now converts via the
>       same `safe_typecast -> bool` path `assert`/`if` use, which
>       tag-dispatches correctly. This removes the whole `Optional[instance]`-
>       in-`not` false-proof class. **Soundness-first tradeoff (landed):** it
>       regresses `re4`/`re11` to TIMEOUT — correctly evaluating
>       `not re.match/fullmatch(...)` makes the solver prove the regex does
>       *not* match (the documented slow negated-membership refinement that the
>       old unsound int-unwrap sidestepped). These are decidability
>       regressions, not false proofs. **Follow-up:** a presence-based `Match`
>       truthiness (or null-guarding `python_truthiness`'s speculative
>       `__class_ptr` derefs) to recover `re4`/`re11` without the membership
>       refinement. Sweep 2947→2946 (+`re10_fail`, −`re4`/`re11` timeout).
>     - **`neural-net_fail` — NOT a false proof (CPython-divergence; corrected
       2026-06-18).** `f = relu(2*0.749 − 3*0.498) + relu(0.749 + 4*0.498)`
       then `assert f >= 2.745`. CPython computes `f == 2.745` exactly, so
       `f >= 2.745` is **`True`** — our `SUCCESSFUL` is correct; the test's
       expected `FAILED` is an ESBMC divergence. (Earlier notes wrongly
       assumed `f < 2.745`.) Confirmed: the GOTO already emits *symbolic*
       `floatbv` ops (`floatbv_minus(floatbv_mult(...))`) — the frontend does
       **not** bake folded float constants into the GOTO; symex does the
       arithmetic and matches CPython.
> - **Category-4 float-folding excision — attempted, reverted (2026-06-18).**
>   Hypothesis: the frontend redundantly host-folds float arithmetic that
>   symex should own. **Empirically false for floats:** the GOTO already
>   carries symbolic `floatbv` (symex owns the arithmetic). The
>   `try_eval_double`/`float_constants` machinery is a conversion-time
>   **category-2** side-channel that supplies *constant* float values to
>   Python-semantic ops symex cannot do — `str(complex)`/`repr` formatting,
>   `cmath.log`/`pow`, `math` intrinsics, `**`. Skipping float folding in
>   `try_eval_double` regressed **17 tests** (`complex_*`, `math4/13/23`,
>   `power16/20`) with zero soundness benefit (no float false proof exists).
>   Reverted. **Lesson:** machine-arithmetic-→-symex already holds for floats;
>   the residual float-value tracking is necessary category-2, not removable
>   category-4.
> - **Integer-path audit (a, 2026-06-18).** Large-int arithmetic and
>   comparisons are *correct* — symex evaluates them symbolically/exactly (the
>   GOTO is not baked). The one real leak: `str()` of a CONSTANT integer ran
>   `try_eval_double` first, whose `< 1e15` guard routed larger ints to the
>   lossy `double`→`ostringstream` path, so `str(9007199515875289)` became
>   `"9.0072e+15"`. **Fixed (`a2f63956a8`):** gate that block to non-integer
>   args; integers use the exact paths (`to_integer` / `cprover_string_of_int`)
>   that already existed below. (Reinforces the rule: integers must never be
>   folded through `double`.)
> - **Global scalar constant-tracking across calls (b) — FIXED (`5cfb036d82`).**
>   A global **string** (or float) written inside a called function was not
>   reflected at a later module-level read: scalar value tracking
>   (`string_constants`/`float_constants`) kept the pre-call value (a false
>   *positive*). Fix (mirrors `invalidate_loop_writes`): a user-function call
>   invalidates the global-keyed scalar value-tracking; symex recovers the real
>   post-call value from the symbol (sound — only drops a now-unsound fold).
>   Locals are kept. (Scalar invalidation only; container-literal maps are
>   read structurally by argument unpacking `f(*c)` and are not touched here.)
> - **global-dict mutation in a function — FIXED (2026-06-18,
>   `170fbe452d` + `849fe1dcda`).** Was a genuine **false proof**: a function
>   mutating a module-global **dict** via subscript-assign (`def f():
>   d["b"]=2`) had the **entire statement dropped** (empty GOTO body), so
>   `assert "b" not in d` / `assert d["a"]==1` *verified* after `f()` set them.
>   (Module-level, local, and by-reference dict **arg** mutation all worked;
>   lists were sound throughout — `g.append(x)` propagates. Not a
>   constant-folding issue.) **Two-layer root + fix:**
>   1. *Type resolution:* function bodies convert in sub-pass 1c *before* the
>      module-level `d={...}` types the global, and Pass 0's global
>      pre-registration lumped Dict literals into the Call-shape branch (typed
>      only calls), so the global was never dict-typed → the
>      `is_python_dict_type` subscript-assign branch was skipped → statement
>      dropped. Fixed by a dedicated Dict-literal case in Pass 0
>      (`python_dict_type(k,v)` from the first constant entry).
>   2. *Stale literal:* with the global dict-typed, `len(d)` reflected the
>      mutation but membership/lookup still folded the stale module-level
>      `dict_literals[d]`. Fixed by invalidating global-keyed `dict_literals`
>      at the POST-argument call site (`convert_call`), so `f(**d)` of the
>      current call still reads it; only `dict_literals` is touched
>      (`f(*c)`/list reads and `**d` unpack all preserved — `pep-448-call-unpack`
>      green). symex recovers the real post-call contents.
>   A naive single-step attempt (pre-argument all-container invalidation) was
>   reverted first — it broke `c`'s repeated `*c` unpacks in `pep-448`. Both
>   commits sweep-neutral (PASS 2946, 0 regressions); regression
>   `global-dict-mutation-via-call`.
> - **Generalised to method + transitive call forms — FIXED (`770a47c15a`).**
>   The per-callee invalidation sites missed `c.m()` (method dispatch is a
>   different path) — a method mutating a global dict/string was still a false
>   proof. Moved invalidation to the **single chokepoint every Call node
>   passes through** (`convert_expression`'s Call dispatch, post-arguments), so
>   free functions, methods, and transitive chains are covered uniformly. A
>   naive "invalidate all globals at every call" there over-invalidated
>   non-mutating calls (`str.upper()`, `len()`) and regressed read-only-global
>   tests; fixed by a pre-pass (`collect_function_global_mutations`) recording
>   names mutated inside *any* function/method (dict subscript-assign,
>   `global X` rebind, dict-mutating method), so only those globals are
>   invalidated — never-mutated globals keep their folding, transitivity is
>   covered by construction. **Architectural note:** conversion-time tracking
>   must be invalidated wherever tracked state can change behind the converter
>   — loops, by-ref args, and now *any call form* via one chokepoint, scoped by
>   a cheap "is this global ever mutated in a function" summary.

> **Refreshed status (2026-06-16).** **P0 (soundness) is empty.** The
> **regex/string precision track is now largely complete on the native
> backend**: precise `re.findall`/`re.split` (incl. `maxsplit`), the
> match-position intrinsics, and `Match.group(n)` capture-group extraction
> (§4 Phase 3, `fcd2001deb`) have all landed, along with the imported-module
> default-population fix (`dda00127e2`, which also benefits `re.sub` and any
> defaulted stub), empty-list annotation typing + the `smt_string`↔scalar
> typecast defensive net (`1c62587a8e`, so §9(b) is resolved), and method
> defaults on optional/union receivers. **One new robustness finding** (while
> wiring `group(n)`): `bounded_nondet_string` always returned an `smt_string`,
> which crashed the refined string solver — `re.sub` on the *default* backend
> cored. **Fixed (2026-06-16, `4e55f1d3a5`); Tier 0 is done.**
>
> **Tiered priority (supersedes the earlier P0–P6 ordering; the detailed
> sections below remain the reference):**
> - **Tier 0 — default-backend robustness. ✅ DONE (`4e55f1d3a5`).**
>   `bounded_nondet_string` is now back-end-aware (refined → `{length,data}`
>   struct, not `smt_string`) and `re.sub`'s precise lowering is gated on
>   native. Closed the `re.sub`-on-refined core and every nondet-string
>   fallback that reached the refined solver.
>   (Latent, native-only, no corpus instance: `smt_string` members in
>   byte-operated structs — [#native-byte-ops](#native-byte-ops); lower.)
> - **Tier 1 — symex-bound timeout cluster. PARTIAL: default list capacity
>   lowered 64->16 (2026-06-17, `0bea6f8e75`).** The cluster (`dict65`,
>   `github_3684`, `list31`, `github_3626`, `github_3667_2`) is dominated by
>   **array field-sensitivity** expanding the fixed list `data` array on every
>   assignment/copy (cost linear in `PYTHON_MAX_LIST_LENGTH`) -- a probe showed
>   the `python_value` tagged-union SSA is only a *secondary* contributor.
>   Lowering the default is sound (the bound is now a *reported* model bound,
>   not silent -- see the capacity note above) and build-tunable
>   (`-DPYTHON_MAX_LIST_LENGTH=N`); it cleared the cluster (sweep 2930->2933,
>   +3, 0 regressions). **Open (principled, keep 64):** lazy/accessed-only
>   array field-sensitivity (the "only expand what we need" answer; a deep
>   goto-symex change -- `field_sensitive_ssa_exprt` needs its fields to cover
>   the whole object) or logical-length array sizing (frontend; array size is
>   part of the list type). Separately, **`python_value` field-by-field SSA**
>   ([§8](#performance)) remains a real cost for *symex-bound* benchmarks that
>   run to completion (e.g. aws_untagged), distinct from this timeout cluster.
> - **Tier 2 — precision substrates (each unblocks a whole group).** Instance
>   **`__dict__` substrate** ([§10](#descriptors): dynamic attrs / shadowing /
>   stateful descriptors / `setattr`; local-variable dynamic-attr discovery
>   landed `9f52de49ee`); **cell substrate** for escaping closures
>   ([§2](#closures)); **nested-container aliasing**
>   ([§9](#nested-aliasing)). **NB the nested-container item is UNSOUND
>   (false proofs), not a precision miss** — anonymous nested mutable literals
>   are stored by value, so repetition/concat/slice/copy/append-element lose
>   CPython aliasing. **The by-reference-at-construction fix was prototyped and
>   found EMPIRICALLY UNTENABLE** (structural `==` of wrapped elements is
>   O(width^depth)×string-solver → 60 s timeout on a 3-element list; see
>   [§9](#nested-aliasing)). Use the proportionate SOUND alternative instead: a
>   taint + element-mutation `python-model-bound` guard that keeps by-value
>   (cheap structural ops) and fires only on the actual unsound pattern (so it
>   is sweep-neutral — the false proof is corpus-invisible today). Do NOT
>   pursue the byref substrate.
> - **Tier 3 — regex/string finish.** On the DEFAULT backend (no `--cvc5`), a
>   constant pattern+subject now decides precisely via a conversion-time matcher
>   folded in the refined solver: **Match()/None** (`aa71a43868`),
>   **inline-flag `(?i)`/`(?s)` patterns + `flags=` IGNORECASE/DOTALL routing**
>   (`31f4cabfa2`), and **Match.start/end/span/group(0) positions + re.sub
>   replace-all** (`c7733c5739`). All +0-regression sweeps (the corpus regex
>   wins came from Match()/None, +12). Remaining: literal-symbolic patterns
>   ([§4](#regex)); **re.findall / re.split stay a sound over-approximation on
>   the default backend** — they chain positions through a loop whose `from` is
>   the previous (solver-symbolic) `end`, which cannot constant-fold at solve
>   time (a single list-returning intrinsic would be needed); the deep
>   refined-solver gap (SYMBOLIC subjects stay sound nondet; precise on native).
>   `flags=` ARGUMENT precision for non-inline use is sound nondet (concat
>   wall; zero corpus value). **Deferred/research-grade:** regex Phase 4
>   (greedy/variable-length group framing), refined-backend regex axioms
>   (Wave 3), symbolic `count`/`rfind` bounded loops (perf-gated).
> - **Tier 4 — capability & breadth.** Async result-binding ([§13](#async),
>   small live bug); module breadth ([§6](#modules), ranked by corpus import
>   frequency); higher-order residual ([§12](#higher-order): named-container
>   callables); icontract multi-level Liskov + C3 MRO ([§11](#icontract)).
> - **Tier 5 — blocked / point precision / maintenance.**
>   `--python-check-annotations` default-on ([§7](#check-annotations), BLOCKED
>   on core); JBMC native `smt_string` (P4); point precision (`complex` C/D,
>   `math` domains, `nondet_list4/5`); P5 re-checks.
>
> **Recommended sequence:** (1) Tier 0 `bounded_nondet_string`; (2) Tier 1
> `python_value` SSA; (3) Tier 2 instance-`__dict__`; (4) the Tier 3 regex
> finishers + Tier 4 async binding (contained, high-visibility); (5) the
> remaining Tier 2 substrates, then breadth.
>
> **Container capacity is now a CHECKED BMC bound (soundness; landed
> 2026-06-17, prerequisite to Tier 1).** `PYTHON_MAX_LIST_LENGTH` (64) was an
> *unchecked* truncation point: append/insert/comprehension reported it
> (`python-model-bound` assert + path cut), but list repetition `l*n`,
> concatenation `a+b`, `extend`, augmented `+=`/`*=`, and `list(iterable)` set
> `length` past capacity and silently filled only 64 data slots, so a valid
> in-length read past slot 63 returned unmodelled nondet (a potential false
> proof). Closed in two layers: producer-side guards (`emit_count_capacity_guard`
> at repetition/concat/extend, `971fc5d860`) report+cut at the growth point;
> and an **access-level catch-all** (`emit_index_capacity_guard` at the
> list-subscript read chokepoint, `26efa370d9`) reports+cuts whenever a valid
> index (`idx < length`) reads past the modelled array, covering EVERY producer
> incl. `+=`/`*=`/`list()` and the read side — and since every *observation* of
> over-capacity data is a read, this is the soundness backstop. A normal
> out-of-range access (`idx >= length`) stays a plain IndexError (not
> misreported); a grown literal array uses its actual size. So lowering/tuning
> the capacity (or logical-length sizing / lazy array field-sensitivity — the
> Tier 1 perf levers) is now sound: hitting the bound is *reported*, like an
> unwinding assertion, and raising the bound clears it. The default
> `PYTHON_MAX_LIST_LENGTH` was lowered 64->16 (Tier 1, `0bea6f8e75`); it is
> `#ifndef`-guarded, so `-DPYTHON_MAX_LIST_LENGTH=N` at build time raises it
> (the array capacity is a compile-time type size, so it cannot be a runtime
> flag — a runtime flag could only ever *lower* the effective bound).
> **Dict/set extended (2026-06-17, `2f31b89204`).** Set was a genuine FALSE
> PROOF (a 64-bit bitmap over `[offset, offset+64)`; an out-of-range element was
> silently dropped, so `100 not in {0, 100}` held): every set element-ADD
> producer (literal, `set(iterable)`, `set.add`) now guards `0 <= elem < 64`
> (`emit_set_range_guard`) — sets cannot use a read backstop since the element
> is lost at construction. Over-capacity dict *literals* are guarded at
> CONSTRUCTION (report + cut + cap), covering downstream reads and iteration in
> one check (chosen over a per-`d[k]` read catch-all, which timed out heavy
> nondet-dict tests); dict insert/comprehension were already guarded.
> **Residual (not soundness gaps):** an *eager* list write-side report (the
> subscript-WRITE paths are scattered; a silent over-cap write is only
> observable via the guarded read — verified); dict `update`-past-cap and set
> comprehension / set binary-ops (`|` of two sets) as lower-frequency producers
> on the same `emit_count_capacity_guard` / `emit_set_range_guard` mechanisms.


**P0 — Soundness (always first).**
- ~~**Return-type inference erases `None` from `X`-or-`None` returns**~~
  ([§0](#false-proofs)): **RESOLVED (2026-06-14)** — imported-module,
  two-statement, and conditional-expression (`IfExp`) forms all preserve `None`
  now; sweep fallout-neutral.
- ~~**`github_3647_9_fail`**: dict-mutation-during-iteration~~
  ([§0](#false-proofs)): **RESOLVED (2026-06-14)** — CPython-faithful size
  check at each `__next__`; sweep PASS 2929→2930, 0 regressions. **P0 is now
  empty.**

**P1 — Native robustness (crash on valid code).**
- ~~**`bounded_nondet_string` returns `smt_string` on every back-end → refined
  string-solver crash**~~ — **RESOLVED (2026-06-16, `4e55f1d3a5`).** The helper
  is now back-end-aware: native → `smt_string` (unchanged); refined → a
  `python_string` `{length,data}` struct nondet with its length pinned to the
  solver-visible length and bound to `[0, PYTHON_MAX_STRING_LENGTH]` (mirrors
  `nondet_str`'s refined path). One fix covers every nondet-string fallback
  site (`re.sub`, the `str`-builtin / `call_method` / `call_user` fallbacks).
  `re.sub`'s precise lowering is also gated on native (it builds an `smt_string`
  intrinsic, the wrong representation on refined). `re.sub` on the default
  backend was coring in `add_axioms_for_length`; it now verifies soundly. Test
  `regex-sub-refined-no-crash` (default backend).
- ~~**`re.*` + `len(str)` model-parse crash**~~ — **RESOLVED (2026-06-15,
  `abbea1ae6d`).** Root cause was an **unbounded** `input()` smt_string: the
  solver could pick a length-2^63 string, wrapping `len()` negative (signed
  64-bit) and forcing a `(witness …)` model CVC5/the parser couldn't read.
  Fixed by bounding `input()` to `[0, PYTHON_MAX_STRING_LENGTH]` under the
  native backend (refined unchanged) + a defensive `parse_struct` empty-string
  fill for `smt_string` components. `re.search(p,s); assert len(s)>=0` now
  verifies SUCCESSFUL and `len(input())>=0` is sound. Test
  `regex-len-native-no-crash`.
- ~~**`smt_string` members in byte-operated structs**~~
  ([#native-byte-ops](#native-byte-ops)): the dict by-reference *mutation* crash
  is **RESOLVED** (the Any-container promote+write-back routes the dict through
  a clean typed view, no byte op); only a latent general `smt_string`-in-byte-op
  gap remains with no corpus instance.

**P2 — Back-end capability parity (two tracks, both first-class).**
- *Refined track — default back-end toward SMT parity (kept, not downgraded):*
  - constant-pattern **regex precision under refinement strings** — the proper
    fix for the `re*` benchmarks now nondet under the default back-end
    (string-refinement regex axioms; [§4](#regex) "Wave 3", research-grade);
  - **membership convergence** (`not_contains` existential-witness
    instantiation) and **lexicographic ordering**;
  - **producing-op precision** (slice / `replace` / `repeat`) under refinement.
- *SMT track — native reach ([§3](#strings)):*
  - **LANDED (2026-06-14):** case mapping `upper`/`lower`/`casefold`/`swapcase`
    (per-char `str.to_code`/ASCII-arithmetic/`str.from_code` + concat);
    `count`/`rfind`/`rindex` (constant subjects fold; symbolic forward
    find/index already lower to `str.indexof`); string `repeat` `s*n` for a
    constant `n` (n native concats). **Structural fix:** native `smt_string` is
    not a struct, so string methods were missing the struct-gated dispatch and
    *all* fell through to nondet (even constants); the fix routes them via the
    `native_supported` set into `try_string_method`. **Perf caveat:** the
    per-char / concat shapes are precise but a *fully-symbolic* subject combined
    with a `len()` query inherits the CVC5 str perf cost (constant /
    assume-pinned subjects are fine; native corpus 0 crashes).
  - still nondet (sound) under native — **fix shapes (2026-06-15 desk plan):**
    - `title` / `capitalize` — **LANDED (2026-06-15, `9b5d3cc174`)** as
      position/word-boundary case maps (title upper-cases the first letter of
      each word, i.e. an index whose predecessor is not an ASCII letter).
    - `strip(chars)` — **LANDED (2026-06-15, `b477ced78d`)**: symbolic
      strip/lstrip/rstrip with a constant char-set, via the whitespace-strip
      maximal decomposition parameterised by a `[chars]` regex class (reusing
      the match/fullmatch intrinsics). `str(float)` — **already sound** (no
      work): literal floats fold; symbolic floats are a sound nondet string.
    - symbolic `count` / backward `rfind`/`rindex` — bounded `str.indexof`
      loops; same shape as the list-valued split loop ([§4](#regex)), but
      perf-heavy on fully-symbolic subjects, so gate/measure before enabling.
    - `split` (list-valued) and `repeat` with symbolic `n` (nonlinear) — see
      the list-valued plan ([§4](#regex)) and keep `repeat`-symbolic nondet.

**P3 — Regex reach (SMT path; [§4](#regex)).**
- **Soundness hardening — LANDED (2026-06-14).** The shared
  pattern→SMT-LIB-regex translator (`src/solvers/strings/python_regex_to_smt`)
  underpins *every* precise regex decision, so a mistranslation is a group-wide
  soundness hole. Fixed a cluster of over-matching bugs (anchors `^`/`$` were
  stripped then ignored; `.` matched `\n`; negation `[^…]`/`\D`/`\S`/`\W` used
  bare `re.comp`, accepting `""` and multi-char strings; control escapes used
  literal `\n` instead of `\u{a}`; `\A`/`\Z` became literals; `{m,n}` with
  `n<m`). Also fixed the **a-prime fallout**: an untranslatable/symbolic pattern
  emitted a definite `bv0` ("no match"), which the post-a-prime stub turned into
  an always-`None` false proof — now a fresh nondet (both branches reachable).
  Tests `regex-translator-soundness`, `regex-unsupported-pattern-nondet`.
- **Still to do (precision)** — plans below (CVC5-validated 2026-06-15 spike):
  - **List-valued ops — `str.split` / `re.split` / `re.findall` (PLANNED;
    `str.split` constant + sound-symbolic LANDED 2026-06-15, `2b405dfef1`).**
    `str.split` now folds precisely for constant subjects under the native
    backend (it was gated out of `native_supported`) and returns a sound
    length-bounded nondet list for symbolic subjects.

    **Sound floor for `re.findall` / `re.finditer` / `re.split` — LANDED
    (2026-06-15, `72ef692453`).** These previously returned `[]`
    unconditionally, which was *unsound* (iterating the result silently checked
    nothing → missed bugs), not merely imprecise. They now return a sound
    bounded nondet list (`nondet_list`; strings for findall/split, `Match`
    objects for finditer), closing the missed-bug class on both backends.
    Length is bounded (8) — a BMC limit like loop unwinding.

    **Match positions — Phase 1 LANDED (2026-06-15, `1e52da2ca5`).** The
    match-position intrinsic (`__cbmc_re_search_start/_end`) is implemented per
    [python-frontend-regex-position-plan.md](python-frontend-regex-position-plan.md):
    `Match.start()/end()` and `group(0)` are precise for fixed-length patterns
    on constant subjects under the native backend (bounded leftmost-start scan,
    spike-confirmed leftmost-sound), with a sound nondet floor for
    variable-length / symbolic / refined-backend (the perf cliff — symbolic
    feasible only to ~16 chars — keeps the precise path gated to constant
    subjects). **Phase 2** (precise `re.findall`/`re.split` via a `from`-driven
    bounded loop) and **Phase 3** (`group(n)` decomposition) build on it. The
    clean `bounded list-return` mechanism — a loop `i in [0, N)` using
    `str.indexof(subject, sep, pos)` for the next separator/match position and
    `str.substr` to extract each segment, accumulating a `list[smt_string]` of
    up to `N` elements (`N` = unwind bound; exact for constant subjects) — uses
    the match **start/end positions** now provided, or the current `__cbmc_re_*`
    intrinsics do not return (bool-only). `str.split` with a *literal* separator
    can use `str.indexof` directly (no regex positions) and is the tractable
    **Phase 1**; `re.findall`/`re.split` Phase 2 requires either adding a
    position-returning regex intrinsic or restricting to fixed-length patterns
    (lowered to `str.indexof` of the literal). `findall` is the dual of split
    (collect matched segments rather than the gaps).
  - **Group extraction — `m.group(n)` (LANDED, `fcd2001deb`).** Models a
    match's groups by **`str.++` decomposition**: a fresh `String` per
    non-literal fragment `gi`, asserting `matched == frag0 ++ frag1 ++ …` with
    each `gi` constrained by `str.in_re` of its sub-pattern, and `group(i)` the
    i-th group's `gi`. **Soundness:** the decomposition *over-approximates* —
    `gi` ranges over every valid split, so reading it explores all framings.
    No uniqueness *gate* is needed for soundness (an earlier plan thought one
    was): a specific-value property like `group(1) == "12"` is provable only
    when the split is uniquely pinned (e.g. `(\d+)-(\d+)` on `"12-34"` — the
    `-` forces `("12","34")`), and stays *unprovable* (sound) when ambiguous
    (`a(.*)b` on `"axxbyyb"`, or `(\d+)(\d+)`), while invariants that hold for
    all framings (`len(group(1)) >= 1` via `in_re`) still prove. The work lives
    in `smt2_conv` (the constant pattern is recoverable only post-constant-
    propagation, not in the front-end for an imported-module stub param); the
    segmenter `python_regex_segment_groups` bails to the nondet floor on
    alternation / quantified / nested / named / non-capturing groups, anchors
    and back-references. Matched text must be pinned: `fullmatch` (whole
    subject) always precise; `search`/`match` precise for fixed-length
    patterns, sound nondet otherwise. See doc/python-frontend-regex-position-
    plan.md Phase 3. PLR: `re` groups are leftmost-longest (the precise subset
    coincides).
  - **Literal-symbolic patterns** (segment list + `str.to_re` holes; anchors
    now soundly modelled, so the embedded-anchor bail is the only caveat).
- **Compilation flags — inline-flag precision LANDED (2026-06-15);
  `flags=` argument precision TODO.** Architecture (per review): flag handling
  stays out of the SMT back-end — the back-end understands the *regex*
  inline-flag syntax `(?i)`/`(?s)` (language-neutral), not a CPython flags
  bitmask. The translator parses a leading inline-flag group and applies
  IGNORECASE (ASCII case folding) / DOTALL (`.`→allchar), bailing to nondet on
  a/L/m/u/x; `extract_literal` folds `str.++` of constants so concatenation-
  built patterns are still recovered; imported-module constant attributes fold
  (re.IGNORECASE → 2). So `re.search("(?i)abc", s)` / `re.compile("(?i)abc")`
  and `(?s)` are precise. The `flags=` *argument* still routes through the
  stub's sound nondet floor (was unsound before — the flag was dropped). The
  clean precise design is: front-end folds the Python flag bits into an
  inline-flag prefix on the pattern. It is blocked by front-end constant-
  propagation: the prefix-building folds for literals but not through the stub's
  `flags` parameter (a param `flags & bit` conditional becomes a symex branch
  whose string merge is not a constant, so `extract_literal` can't recover it),
  and the C++ front-end only sees a conversion-time constant at the direct call
  site, not through the stub. Closing it needs either call-site folding of
  `re.<method>(p, s, re.X)` (where `re.X` already folds) or a constant-fold of
  the stub's prefix construction.
- **`re.sub`/`subn` — LANDED (2026-06-14).** Precise via `str.replace_re_all`
  on the sound subset only: CVC5's `str.replace_re_all` is leftmost-*shortest*
  whereas CPython `re.sub` is greedy (leftmost-longest), so they coincide iff
  the pattern is constant, anchor-free and **fixed-length ≥ 1** (forced match
  length ⇒ greedy = shortest, no empty matches), the repl is a constant literal
  (no back-references/escapes), and `count == 0`. Outside the subset → sound
  nondet string. Added `python_regex_fixed_length`/`python_regex_to_smt_body`,
  the `cprover_string_re_sub_func` intrinsic, and three general call-dispatch
  fixes (the stale `re` catch-all no longer shadows sub/subn; module and
  positional class-method dispatch now apply trailing default arguments).

**P4 — Cross-front-end (Java).**
- **JBMC native-SMT-string mode**: `java.lang.String → smt_string`, reusing the
  shared `smt_string` type + `str.*` lowerings + `expr_initializer` /
  `boolbv_width` / `pointer_logic` handling (a spike adds Java front-end wiring;
  no shared-code shift needed).

**P5 — Maintenance / re-checks.**
- CVC5 perf edge: `str.in_re` + `len()` on the same symbolic subject can time
  out. Re-check string-keyed dict workarounds ([§9](#precision), e.g. the
  `counter == -1` / `popitem()` items) under **both** back-ends.

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

Net: the frontend is effectively free of genuine false proofs; the
previously-deferred `github_3647_9_fail` (dict-mutation-during-iteration) is now
**RESOLVED** (below), as is the return-type `None`-erasure vector — so there are
no open deliberately-deferred soundness items at present.

### Return-type inference erases `None` from `X`-or-`None` returns — HIGH PRIORITY (2026-06-12)

### Return-type inference erases `None` from `X`-or-`None` returns — RESOLVED (2026-06-14)

**Was a latent unsoundness (false-proof vector).** A function/method with **no
return annotation** whose body returned a concrete type `X` on one path and
`None` on another could have its return type inferred as `X` with the `None`
branch coerced away, so the function could never return `None` — making a
caller's `None` guard dead code and missing bugs on the `None` path (e.g.
`f(x).attr` when `f` returns `None`).

**Resolution.** Closed across three forms, each an architectural (whole-group)
fix:
- **Imported-module functions** (commits `8c0c50fc62`/`3c08c41f21`):
  `process_imported_module` had its own partial inference; rerouted through the
  shared `infer_return_type_from_body`, which already widens to the tagged
  union when any return path yields `None`.
- **Two-statement `if c: return X` / `return None`**: already correct via that
  shared widening (`has_value_return && has_none_return` → `python_value`).
- **Conditional-expression `return X if c else None`** (commit *"preserve None
  in conditional-expression (IfExp) returns of class type"*, 2026-06-14): the
  remaining gap. Two layers fixed: (1) `convert_if_exp`'s branch-merge
  `category()` now classifies class-instance / tuple / set / complex structs, so
  a class-vs-`None` conditional wraps into the tagged union (preserving `None`)
  instead of `safe_typecast`ing `None` to the class type — fixing the whole
  class-vs-{`None`,int,str,…} group; (2) `infer_return_type_from_body` now
  flattens nested `IfExp` into its leaf return-values and analyses each arm, so
  `C() if c else None` infers Optional and `[1]/{…} if c else None` infer the
  right container-or-`None`.

**Scope.** Only *inferred* return types were affected; `Optional[...]` /
`X | None` annotations were always honoured. Fallout was expected (callers
relying on the erased-`None` exploring the real `None` path) but measured
**zero** on the ESBMC sweep (the only broken form, IfExp, is not exercised by
the corpus in a relied-upon way). Guarded by `return-ifexp-class-or-none`
/`-falseproof` and `import-optional-return-inference`.

### Any-typed mutable-container by-reference mutation is lost — HIGH PRIORITY (2026-06-12)

### Any-typed mutable-container by-reference mutation — FIXED for element mutation (2026-06-12)

**Status: RESOLVED for dict/list element mutation** (commit *"python: propagate
by-reference mutation of containers passed to Any params"*). A residual gap
remains for length-changing methods (e.g. `list.append`) — see below.

**The bug (was a latent false proof, both back-ends).** A mutable container
(dict/list) passed to an **unannotated / `Any` (`python_value`) parameter** and
mutated by the callee did **not** propagate the mutation back to the caller —
the caller's object kept its stale pre-call value — and a post-call read folded
against that stale value, proving a false assertion:

```python
def f(d):          # d unannotated -> python_value parameter
    d["k"] = 9
m = {"k": 1}
f(m)
assert m["k"] == 1   # was: VERIFICATION SUCCESSFUL (WRONG: CPython has 9)
assert m["k"] == 9   # was: VERIFICATION FAILED   (WRONG: should hold)
```

**Root cause.** The `Any` path wrapped the argument via `wrap_value`, which
materialises a throwaway *copy* of the container into a temp and wraps its
address with **no write-back** — so the callee's mutation (applied through
`python_value.__class_ptr`) hit the discarded copy. The caller's
constant-tracking for the argument was also not invalidated, so the post-call
read folded against the literal. (The **annotated** control `def f(d: dict): …`
was always correct: a concrete-container parameter is a by-reference pointer
that propagates, via `safe_typecast`'s struct→pointer promotion + post-call
write-back, and `convert_user_call` already invalidated its tracking.) Under
`--python-smt-strings` the same shape *crashed* in `lower_byte_operators` /
`unpack_struct` (byte-unpacking the `smt_string`-keyed dict reached via the
opaque `__class_ptr` cast — see [#native-byte-ops](#native-byte-ops)).

**The fix (sound + precise).** Route a mutable-container *lvalue* argument bound
to an `Any` parameter through the **same** promote + post-call write-back
boundary the concrete by-reference path already uses (`safe_typecast`
struct→pointer), promoting to the **exact** canonical layout the `python_value`
subscript handlers assume (`dict[str, value]` / `list[value]` — matching
`python_dict_type(python_string_type(), python_value_type())` in
`python_converter_assign.cpp`), then wrapping the resulting pointer into the
tagged union via `make_python_value`. The caller's constant-tracking for the
argument is invalidated. Because the promoted temp is a clean, field-sensitive
typed object, this *also* eliminates the native byte-op crash (the opaque
`__class_ptr` now points at a properly-typed `dict[str, value]`, which symex
handles field-sensitively rather than byte-reinterpreting). Two lessons that
shaped the fix: (1) an invalidation-only attempt was insufficient — the loss
was at the value level, not just the constant-fold; (2) using `dict[value,
value]` for the promotion silently corrupted via type-punning — the canonical
key type must be the **string** type the handlers assume, not `value`.

Validated: dict + list element mutation propagate precisely under refined and
`--cvc5 --python-smt-strings`; the false proof is gone (asserting the stale
value now FAILS — `regression/python/any-param-dict-byref-falseproof`);
positive propagation guarded by `any-param-dict-byref` /
`any-param-list-byref`; native crash-scan 0 crashes; ESBMC sweep
fallout-neutral (PASS 2929, 0 regressions).

**Container methods on an `Any` parameter — RESOLVED for unambiguous built-in
methods (2026-06-13).** `x.append(y)` / `extend` / `insert` / `sort` /
`reverse` and `dict.setdefault` / `popitem` / `keys` / `values` / `items` on an
`Any`-typed parameter now propagate (`a=[1,2]; g(a) [g(x): x.append(99)];
assert len(a)==3` verifies; `len(a)==2` correctly FAILS). The architectural
fix is a single shared helper `unwrap_any_container_receiver(obj, method_name)`
applied at the method-receiver conversion sites
(`python_converter_call_method.cpp` dispatch + the statement-level
append/insert handlers in `python_converter_defs.cpp`): when the receiver is a
`python_value` and the method name unambiguously belongs to one built-in
container — *and no user class defines it*, so virtual dispatch is preserved —
it derefs the shared `__list_ptr` / `__class_ptr` to the concrete
by-reference container lvalue (`list[value]` / `dict[str, value]`), and the
existing container-method handlers mutate it; the caller-side promote +
write-back boundary then carries the change back. Guarded by
`any-param-list-append{,-falseproof}`, `any-param-list-extend`,
`any-param-userclass-method-collision`.

**Ambiguous method names — RESOLVED via runtime `__tag` dispatch (2026-06-13).**
Names shared across containers — `pop` / `remove` / `clear` / `copy` /
`update` — are dispatched by `dispatch_any_container_method_by_tag`: each
candidate container's handler runs on its by-reference view and its emitted
effects + result are guarded by `python_value.__tag == <container>` (via
`guard_pending_checks`), so exactly the live container is mutated at runtime.
Guarded by `any-param-list-pop{,-falseproof}`, `any-param-dict-pop-clear`,
`any-param-userclass-pop-collision`.

**`set` arguments — RESOLVED (2026-06-13).** A `SET` tag was added to
`python_type_tagt`; a set (an element-type-agnostic fixed bitmap struct) is
shared by direct address via `__class_ptr` (no promotion / write-back needed),
and the unwrap + `__tag`-dispatch paths gained a set view. `add` / `discard` /
`remove` / `clear` on an `Any`-typed set now propagate. Guarded by
`any-param-set-add-discard{,-falseproof}`, `any-param-set-clear-remove`.

**Remaining gap in this family.** **Non-string-keyed dicts** — the
`python_value` dict handler assumes string keys (`dict[str, value]`), so
int-keyed dicts via `Any` neither propagate nor crash (sound-imprecise). This
should reuse the same promote+write-back + unwrap boundary with a value-keyed
canonical layout (gated, since value keys reintroduce the string-behind-pointer
cost noted in [#dict-byref](#dict-byref)).

### Earlier triage history (2026-06-08)

From the prior per-test triage of the 26 baseline DIFFs in the
*expected-FAILED / got-SUCCESSFUL* direction: **20 were out-of-scope**

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
- **`github_3647_9_fail` — dict mutation during iteration — RESOLVED
  (2026-06-14).** `for k, v in d.items(): d["x"] = 3` raises `RuntimeError`
  ("dictionary changed size during iteration") in CPython; we previously
  verified it SUCCESSFUL (a missed bug / false proof). **Fixed** by a
  CPython-faithful size check at each `__next__`: detect the iterated dict from
  the AST (the receiver of items/keys/values, or the directly-iterated dict),
  snapshot `len(d)` at loop entry, iterate `snapshot+1` times (statically
  bounded by `PYTHON_MAX_DICT_SIZE+1`), and raise `RuntimeError` at the body
  top + a synthetic terminal probe when `len(d) != snapshot`. Sound + precise:
  a value-update `d[k]=v` keeps `len(d)` (no fire), a user `break` exits before
  the next `__next__` (no fire), add/del fires. Required two prerequisite fixes
  to precise parameter-dict iteration (the bounded loop + dereference-in-place
  aliasing). Sweep: PASS 2929→2930, `github_3647_9_fail` DIFF→PASS, 0
  regressions. Guarded by `dict-changed-size-during-iteration` and
  `dict-iter-value-update-no-runtimeerror`. **Residual (P2/§8, not soundness):**
  under the refined default a *symbolic string-keyed* value-update inherits the
  string-refinement performance cliff (correct but slow; fast under
  `--python-smt-strings`).
  <details><summary>Historical investigation (root-cause trail)</summary>

  The blocker was first mis-attributed to the dict-assign `__dict_found_` scan;
  deeper tracing showed it was parameter-dict *iteration*: an unconstrained
  symbolic `*d.length` loop bound (spurious unwinding-assertion failure) plus
  iterating a `__iter_tmp_` snapshot copy of `*d` not aliased to the
  pointer-mutated object (so `d[k]=v` spuriously appended, runaway length
  growth). Both fixed before applying the check.
  </details>

  <!-- superseded prototype notes removed; see commit history -->
  Prototype-era notes: the check was first prototyped, reverted when it
  surfaced the parameter-dict iteration imprecision (see the collapsed
  root-cause trail above), then re-applied after the two prerequisite fixes
  landed.

Net: all 4 genuine false proofs from the 2026-06-08/09 triage are now closed
(`github_3647_12_fail`, `github_2897_2_fail`, `class-attributes_fail`, and
`github_3647_9_fail`). The
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

**Status: LANDED for the closure cell substrate and container dispatch
— named escaping closures (read-only and `nonlocal`-mutating, full
multi-call PLR fidelity), higher-order dispatch through container
subscripts, and comprehension late-binding. The remaining higher-order
cases are characterised below: most are decidable-but-unhandled (future
work, currently sound nondet); a residual core is genuinely undecidable
(sound nondet is the only correct answer).**
This was always a **sound precision gap, not an unsoundness**:
*non-escaping* closures were already correct (late binding within the
defining scope `x = 10; g = lambda: x; x = 20; g()` → 20, and `nonlocal`
mutation via the `qualify_name` redirect). An *escaping* closure
(returned/stored, called later) over-approximated its captured free
variables to **nondet** — a false positive, never a false proof.

**Landed (`038067bc4e`, `02d1495520`).** The carry mechanism + heap-cell
substrate for closures bound to a NAMED variable:

- **Read-only escaping capture** (`def f(): x = 5; return lambda: x;
  g = f(); g() == 5`). The factory-assign rewrite previously bypassed
  `f`'s body, so captured *locals* stayed nondet. Now `f`'s body is run
  (its code-typed return discarded) and each captured local is
  snapshotted at the call site. Lambdas are expressions and cannot
  reassign a capture, so this is inherently read-only/sound; the
  snapshot equals the factory's final value = the late-binding value.
- **`nonlocal`-mutating escaping capture** (counter / accumulator
  factories). The mutated nonlocal becomes a per-invocation **heap
  cell** (`side_effect` `ID_allocate`, initialised from the enclosing
  value at the nested def's site, pointer `python::<parent>::__cell_<v>`);
  the nested body dereferences a pointer capture-param instead of the
  `qualify_name` shared-symbol redirect. The cell persists across calls
  (the counter advances) and each factory invocation gets a fresh cell.
- **Per-closure-variable binding** (`closure_var_captures`, keyed by the
  target name) replaced the destructive shared-`closure_captures` rebind,
  so `g1 = make(); g2 = make()` bind **independent** snapshot temps /
  cells. This is the soundness-critical fix: an unsound-if-shared
  assertion (`g2() == 3` when fresh) correctly FAILS, two independent
  counters verify, and multi-call read-only factories are now precise.

Sound gating: heap cells are applied only when the cell-var is assigned
before the nested def and not reassigned at/after it (so the def-site
init equals the late-binding value); anything outside that window falls
back to the existing nondet path.

**Container dispatch + comprehension late-binding (`4adb3bddbf`,
`ce5dfb6284`).** Calling a callable obtained by subscripting a
statically-known container — an inline/named list (`fns[i]()`), a named
dict (`d[k]()`), and the comprehension `[lambda: i for i in range(3)]` —
now dispatches: `convert_call` resolves the container's element
callables and a constant selector folds to one, while a symbolic
selector over a finite known element set becomes a guarded dispatch
(IndexError/KeyError on no match). Arguments are coerced to the callee's
parameter types and closure captures are appended. Comprehension element
closures observe the loop variable's FINAL value (Python-3 late binding)
via a unique per-comprehension symbol that does not clobber an enclosing
same-named variable. A code-typed capture (a closure capturing another
closure/function value) is left bound from its source rather than
snapshotted (avoids a symex abort; degrades to sound nondet).

**Remaining higher-order closure cases.** Characterised by measurement:

- **Decidable but unhandled** (sound nondet today; future work, not
  inherent limits):
  - *Capture-through-param*: `apply(make())` where `apply(f): return f()`
    and `make()` returns a capturing closure. A direct `apply(lambda: 5)`
    already works (the [§12](#higher-order) param-callable binding); the
    call-returning-closure argument is not resolved and the capture is
    not carried into the specialised callee.
  - *Append-built containers*: `fns = []; fns.append(lambda: 5);
    fns[0]()` — `list_literals` does not track appended callables, so the
    dispatch cannot enumerate the element set.
  - *Instance-attribute closures*: `self.f = lambda: 42; c.f()` — the
    closure stored in an instance field is not resolved at the call.
  - *Closure capturing a closure* (`compose`/`twice`): graceful (no
    abort) but nondet; the code-typed capture is not carried.
- **Genuinely undecidable** (sound nondet is the only correct answer):
  the closure's *identity* is not statically determinable — chosen by
  `random`/nondet from an unbounded set, read from external input, or a
  container whose contents are not tracked. Guarded dispatch is possible
  only over a finite, statically-known candidate set; beyond that, nondet
  is sound and required. (`fns[k]()` with symbolic `k` over a *known*
  list IS decidable and handled.)

**The whole-group fix** for the decidable-but-unhandled cases is a
*fat-closure* representation (the closure value carries its captures
per-instance, so they travel through any channel). Two per-site attempts
at capture-through-param produced false proofs (shared-symbol read; then
cached-clone staleness), establishing that the captures must travel with
the value. The full design and a phased, validation-gated implementation
plan are in
[doc/python-frontend-fat-closure-plan.md](python-frontend-fat-closure-plan.md).

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

**Refined-string precision pass (2026-06-11, landed).** A "sound + precise,
across the board" pass over the refined-string backend landed its
cleanly-achievable wins and characterised the rest by measurement. Landed +
validated (ESBMC sweep: 0 regressions, PASS 2916→2935): symbolic
`rfind`/`rindex` → `last_index_of` (`ee0b89d939`); a dedicated
Python-whitespace `strip`/`lstrip`/`rstrip` axiom (`6cabf82209`, *not* a
`trim` reuse, which is unsound for control bytes). Measured/root-caused as
blocked (kept sound): ordering via `compare_to` (axiom a3's existential
first-diff-index witness isn't instantiated by the refinement); substring
`replace` (existing axiom is char-only); `split` (list-valued); membership
convergence (already handled at HEAD — eager instantiation measured as a net
negative and reverted). **Net: the refined-string precision frontier is
largely tapped; the remaining gaps need either existential-witness
instantiation or new nonlinear/list-valued axioms, for which the SMT-String
backend is the comprehensive answer.** Full per-step ledger:
[python-string-phase2-backend-abstraction.md § consolidated outcome ledger](architectural/python-string-phase2-backend-abstraction.md#string-correctness-plan--consolidated-outcome-ledger-2026-06-11).


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
unroll) for static-value leaves, while produced/heap-backed content uses
association (a Java migration was assessed and found **not applicable** —
Java's strings are heap-backed and genuinely need association; the
front-ends converged on association for produced content). **Phase 1 landed
(2026-06-10):** symbolic `chr`
content equality/contains and symbolic-`chr` concat chains
(`github_3090_4/_5`) prove soundly and loop-safely, zero sweep regressions;
see the design-decision section. **Phase 2 landed (2026-06-10):**
refinement-produced results (concat, substring, `str(int)`, ...) get fresh
per-execution real backing installed in the symex const-prop handlers
(Python-gated; JBMC `jbmc-strings`/`strings-smoke-tests` green), so
byte-level/chained ops on them — `(chr(i)+"oo")[0]`, iteration of a concat
result — prove and are loop-safe; zero sweep regressions. Implementation
discipline + sequencing in the
[design-decision section](architectural/python-string-phase2-backend-abstraction.md#design-decision-2026-06-10-choice-b--symex-content-pointer-resolution).
The SMT-string backend (`--python-smt-strings` / CVC5) remains an orthogonal
precision option.
Full analysis (JBMC loop handling, `find` fast path, storage options,
symex-deref pros/cons, prototype results):
[python-string-phase2-backend-abstraction.md](architectural/python-string-phase2-backend-abstraction.md#update-2026-06-10--corrected-conclusion--symex-deref-feasibility).

**Plan (5 phases; phases 1–2 designed, 3–5 open):**

> **Superseded (2026-06-11).** The single current plan for all deferred string
> work — the `smt_string_typet` refactor (Plan A), interim SMT model extraction
> (Plan B), and the refined-backend axioms replace/repeat/strip(chars)/split/
> casefold/count and the compare_to existential (Plan C1–C6) — lives in
> [python-string-phase2-backend-abstraction.md § Consolidated forward plan](architectural/python-string-phase2-backend-abstraction.md#consolidated-forward-plan-2026-06-11--supersedes-earlier-scattered-plans).
> The 5 phases below are retained for the site inventory (phase 1) only.


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
2. **Library `Match`/`None` result not tied to the intrinsic — RESOLVED
   (2026-06-17, `aa71a43868`); no flag.** The `re` stub now branches on the
   intrinsic (`if __cbmc_re_match(p,s): return Match() else: return None`), and
   the **default (refined-string) backend decides a CONSTANT pattern + CONSTANT
   subject precisely** so the branch is exact (matched → `Match()`, proven
   no-match → `None`) without `--cvc5`. This needed neither a flag nor the deep
   array_pool work feared here: a conversion-time backtracking matcher
   (`python_regex_match`, supported subset; conservative `std::nullopt` →
   sound nondet) is invoked at SOLVE time inside the refined solver's
   `match/search/fullmatch_func` handler (the re stub body is converted once
   with symbolic params, so the literals are only available post-symex, where
   `get_string_expr(array_pool,·).content()` yields the constant bytes). A
   SYMBOLIC subject on the default backend stays a sound nondet Match-or-None
   (precise on native via `str.in_re`). Sweep PASS 2930→2945, 0 regressions, 12
   regex tests (`re1/3/4/5/6/8/9/10/11/12`, `github_3013/_2`) DIFF → PASS.
   (The old `--python-strict-re-result` flag idea is dropped — the default is
   now both sound and precise for the decidable case.)

**Update (2026-06-12) — native SMT-String backend.** With `--python-smt-strings`
now selecting the native `smt_string` representation (the byte-array hybrid is
retired), the **subject** side of gap 1 is resolved: a symbolic subject is
already an SMT `String`, so `(str.in_re <symbolic-subject> <RegLan>)` is precise
with no `array_pool` extraction. One prerequisite fix: the
match/search/fullmatch lowering's `extract_literal()` recognises only the
refined `{length, address_of(array)}` struct, so under native — where the
pattern is an `smt_string` *constant* — it returns `nullopt` and degrades to
nondet. Teaching it to read the pattern from an `smt_string` constant restores
regex precision under native (the **native regex pattern-extraction fix**;
small, prerequisite for everything below).

**Extension — structurally-constant patterns with symbolic literal substrings
(native).** The pattern must remain *structurally* constant (its regex
operators known at conversion time): SMT-LIB `RegLan` is built only from regex
constructors (`re.union`, `re.*`, `re.range`, `str.to_re` of literals) and has
no operation that interprets a *symbolic* string as a regex — `str.to_re(p)`
accepts exactly the literal `p` (i.e. equality, not pattern semantics), so a
fully-symbolic pattern degrades soundly to nondet. **But** a pattern whose
*structure* is a compile-time constant while its *literal substrings* are
symbolic — e.g. `re.compile("^" + prefix + "[0-9]+$")` with `prefix` a runtime
`str` — is expressible as
`(re.++ (str.to_re prefix) (re.+ (re.range "0" "9")))`: `str.to_re` on the
symbolic literal "holes", `re.*` constructors for the constant structure.

  *Design.*
  - **Front-end:** when an f-string / `+`-concatenation forms a regex pattern,
    carry it not as one flattened literal on the intrinsic but as a **list of
    segments**, each either a constant pattern fragment or a symbolic
    `smt_string` literal-hole. (Today the pattern is flattened to a single
    literal, which loses this structure; a new intrinsic variant would take the
    segment list.)
  - **Backend** (`python_regex_to_smt.cpp` + the smt2_conv lowering): translate
    constant fragments as today and emit `(str.to_re <hole>)` for each symbolic
    hole, splicing them into the `RegLan` term with `re.++`.
  - **Soundness / scope:** a symbolic hole is matched **literally** (spliced
    verbatim as a `str`), which is exactly the intended semantics for the
    `re.compile("..." + x + "...")` / `re.escape(x)` idiom. A hole meant to
    carry regex *metacharacters* is out of scope (that is a fully-symbolic
    pattern → nondet). Constant-only and fully-symbolic patterns are unchanged.
  - **Effort / ordering:** front-end segment-tracking is the bulk; the backend
    splice is small. Builds on the native pattern-extraction fix and the
    `re.*`-wrapper routing ("a-prime") refactor, so it is sequenced after both.

- **Compilation flags** (`re.IGNORECASE` etc.): currently fall back to
  nondet. *Fix shape:* rewrite the regex AST per flag before lowering.

**Implementation findings (2026-06-12).**

- **Native regex pattern-extraction fix — LANDED** (commit `5231f3b61d`).
  `__cbmc_re_{match,search,fullmatch}` is now precise under
  `--cvc5 --python-smt-strings` for a constant pattern over **both constant and
  symbolic subjects** (incl. character classes), via `str.in_re` directly on
  the `smt_string` subject. This closes the old "symbol subjects fall through to
  `bv0`" gap (gap 1) for the native backend — the refined bridge is no longer on
  the path. (`string-smt-native-regex`.) A CVC5 perf edge remains: combining
  `str.in_re` with a `len()` query on the same symbolic subject can time out;
  match/no-match decisions themselves are fast.
- **a-prime is *result-precision*, not *routing*.** The `re.*` stub **already
  calls** `__cbmc_re_*` (for the SMT side-effect). The remaining work is to make
  the returned `Match`/`None` *reflect* the intrinsic result. This is entangled:
  the current always-`Match()` is itself a latent **unsoundness** (it never
  explores the `None` path, so a missing-`None`-guard bug such as
  `re.match(...).group()` on a non-match is not caught), but switching to real
  `Match`/`None` makes the result **nondet under the default backend** (the
  intrinsic is nondet there), which changes many benchmark outcomes. So a-prime
  needs an opt-in `--python-strict-re-result` flag (off by default) and a
  stub→flag mechanism, not just a stub rewrite. Higher-stakes than the doc
  implied; prerequisite for the literal-symbolic and `re.sub` items having
  real-world reach.
- **Literal-symbolic patterns: anchor-soundness caveat.** The fragment-wise
  composition (above) must handle `^`/`$` only at the *whole-pattern*
  boundaries; a `^` at the start of a non-first fragment or `$` at the end of a
  non-last fragment must **bail to nondet** (not be stripped per-fragment),
  otherwise the regex is over-permissive (unsound). The translator currently
  strips leading-`^`/trailing-`$` per input string, so a body-only fragment
  translator + boundary handling is required.
- **`re.sub` native:** expressible via CVC5 `str.replace_re_all` (a new
  `__cbmc_re_sub` intrinsic + `str.replace_re_all` lowering), but also entangled
  with the stub (`sub` returns `""` today) and only reaches real code via
  a-prime-style routing.

---

## Native robustness: smt_string members in byte-operated structs  {#native-byte-ops}

**Status: PARTLY RESOLVED — the dict-by-reference-mutation crash is fixed; a
general byte-op gap remains for other shapes (native only; sound — crashes,
never a false proof).**

The original trigger — **dict pass-by-reference *mutation*** through an
`Any`/`python_value` parameter (`def f(d): d["k"]=v` then asserting the caller
sees the mutation) — **no longer crashes** and now propagates *precisely* under
native (see [the §0 by-reference fix](#false-proofs)): the argument is promoted
to a clean, field-sensitive `dict[str, value]` temp instead of being reached
via a byte-reinterpreting opaque `__class_ptr` cast, so no `byte_extract` /
`byte_update` is generated for it.

The underlying lowering limitation is still present for *other* shapes: a struct
that embeds an `smt_string` member (e.g. `python_value.__str`, or a class/dict
struct holding a string) cannot be **byte-operated**
(`byte_extract`/`byte_update` → `lower_byte_operators`), because `smt_string`
has no fixed bit-width and the lowering requires non-constant-width members to
come last: `lower_byte_operators.cpp` fires *"members of non-constant width
should come last in a struct"*. The `regression/python` corpus (543/543 under
native, 0 crashes) does not currently exercise a remaining instance, so it is
latent. Candidate fixes (both shared-code, non-trivial):
(a) teach `lower_byte_operators` to treat `smt_string` members opaquely (NB: a
naive "replace the unlowerable byte op with a fresh nondet" is **unsound** when
the byte op is a write whose effect must alias a caller object — it silently
drops the mutation; only safe for genuinely value-less reads);
(b) order `smt_string` members last in the affected struct layouts. Lower
priority than the regex items; recorded so it is not mistaken for soundness.
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
*Prioritisation:* order new stubs by **import frequency in the ESBMC
benchmark corpus** (`~/esbmc.git/regression/python`) — count `import`/`from`
occurrences and model the most-imported unmodelled modules first; this turns
an open-ended breadth task into a ranked queue.

---

## 7. `--python-check-annotations` default-on blockers  {#check-annotations}

**Status: BLOCKED** on two CBMC-core issues; the flag stays off-by-default.
The annotation checker itself is implemented and correct when it runs.

- **Issue 1 — `boolbv_map` width mismatch** (`boolbv_map.cpp:68`
  invariant). A symbol re-created with a different width trips the map's
  consistency invariant. *Fix direction:* either type-check on lookup, or
  force consistent width at symbol creation in the frontend. *Feasibility
  (2026-06-15):* confirmed — the invariant is exactly `literal_map.size() ==
  width` (bit-vector width consistency), so the frontend-side fix (a symbol
  must keep one width across its re-creations) is the sound lever; still
  **BLOCKED** in that the change lands in flattening or in frontend symbol
  creation, not in the annotation checker.
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

- **String-refinement cliff — effectively RESOLVED (verified 2026-06-15).** The
  string-keyed dict scan / value-update blow-up that was the dominant
  refined-backend limitation is gone after the §5 Option-A inlining of the
  refined string into `python_value`: an 8-key string-keyed dict scan at unwind
  10 runs in ~2.5 s on *both* back-ends. (The separate "`str.in_re` + `len()`"
  item was misfiled here — it is the P1 model-parse crash, see the worklist.)
- **`python_value` SSA expansion — now the top target (spike-confirmed).**
  Field-by-field SSA on tagged-union structs dominates, and the 2026-06-15
  TIMEOUT re-profile ([§9](#precision)) pins it as the cause of the
  frontend-bound timeouts (`dict65`, `shedskin`, `github_3684` time out in
  `--program-only` conversion alone). *Fix shape:* cap `field_sensitivity`
  recursion depth, or emit struct-level (not per-field) SSA for tagged-union
  assignments. Highest-leverage perf item.
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
alarms). The nested-mutable-aliasing item below WAS a false proof; it is now
**guarded for the common cases** (2026-06-17) with a documented residual — see
it for the details. Verified against the 2026-06-08
sweep baseline.

### Nested mutable element aliasing — false proof, now GUARDED (proportionate sound fix landed 2026-06-17) {#nested-aliasing}

**This was a soundness bug, not a precision miss** (corrects the earlier
classification of `github_3667` as a sound precision miss). PLR object
identity: a Python list element that is itself a mutable container is held by
*reference*; replicating or sharing it aliases the SAME object. The frontend
stores anonymous (un-named) nested mutable literals **by value**, so every
operation that replicates or shares such an element produces independent
copies and **wrongly proved** programs that rely on (or are bitten by) the
aliasing. Confirmed false proofs (2026-06-17, were all `VERIFICATION
SUCCESSFUL` where CPython raises `AssertionError`; now reported via
`python-model-bound`, see the landed fix below):

```
g = [[0,0]]*3 ; g[0][0]=1 ; assert g[1][0]==0        # repetition
a=[[0,0]] ; b=a+a ; b[0][0]=1 ; assert b[1][0]==0    # concatenation
a=[[0,0]] ; a.append(a[0]) ; a[0][0]=1 ; assert a[1][0]==0   # append-element
a=[[1],[2]] ; b=a[:] ; a[0].append(9) ; assert b[0]==[1]     # slice copy
a=[[1],[2]] ; b=list(a) ; a[0].append(9) ; assert b[0]==[1]  # list() copy
g=[{}]*3 ; g[0]['k']=1 ; assert 'k' not in g[1]      # dict-element variant
```

The **named** variants are already correct (`row=[0,0]; g=[row]*3; …` →
`VERIFICATION FAILED`): `collect_escaped_mutables` marks a name that appears as
a list/dict element as escaped and promotes its storage to a pointer
(`make_python_value(LIST, &symbol)`), which replicates correctly. Distinct
literals, comprehensions, scalar repetition, and flat lists are all correct.
So the gap is precisely **anonymous mutable literals stored by value**.

**Architectural fix (validated, then reverted — needs completion).** The
whole-group fix is representational, at the single chokepoint of *literal
construction*: route anonymous mutable-container elements through the same
by-reference path the heterogeneous-list branch already uses (`wrap_value` →
`make_python_value(TAG, &symbol)`), so all downstream copy/replicate
operations duplicate the pointer (correct aliasing) for free — one change
fixes the entire group. A one-line spike in `convert_list`
(`python_converter_expressions.cpp`: force the `is_heterogeneous` wrap path
when `is_python_list_type(elem_type)`) **closed every false proof above AND
turned `github_3667` precise** (shallow copy now shares inner lists), with the
read path (`g[i][j]`) still precise.

**Why it is not yet landed (measured fallout).** Python lists have *reference*
element semantics but *structural* `==`. By-reference storage breaks the
value-semantics operations that assumed inline structs: the byref sweep showed
**9 regressions** — structural equality `[[1]]==[[1]]` (`list-eq1/2/6/9`),
`list_extend13/14/16`, `list_depth_test`, `github_3238` — **plus a new crash**,
net PASS 2930→2925.

**Step 1 (structural equality) — SOLVED via static tag dispatch, LANDED
2026-06-17 (`be4131cde6`).** A first prototype of `python_value_structural_eq`
that emitted ALL tag branches per element (string-solver `str_eq` + LIST-deref)
at every recursion level was O(width^depth) × string-solver cost and **timed
out at 60 s on a 3-element list** — which initially looked intrinsic. The fix
is **static tag dispatch**: when an element's tag is a compile-time constant
(the dominant literal / known-structure case) build ONLY that branch, with no
eager string-solver/deref emission for the dead branches. That collapses the
same case to **0.05 s**, and nested literals (`[[1],2]==[[1],2]`) are precise
at <0.2 s. Symbolic-tag elements use a sound bounded fallback (scalars exact;
STR/LIST by **identity-or-nondet** — same pointer ⟹ equal, else nondet;
DICT/SET/deeper ⟹ nondet), which is strictly MORE sound than the old field-wise
`equal_exprt` (that treated distinct pointers as not-equal, unsound for `!=`).
Sweep-neutral (PASS 2933, 0 regressions); fixes the latent heterogeneous
nested-equality precision bug as a bonus. **So the equality blocker is removed**
— the earlier "by-reference is intrinsically untenable" conclusion was wrong; it
was the naive all-branches encoding, not by-reference per se.

**Remaining byref blockers — re-measured under byref + cheap-equality
(2026-06-17).** With the static-dispatch equality in, re-applying the
`convert_list` wrap and running the previously-regressing tests shows the
remaining work is NOT "apply the equality pattern to extend/depth" but a
**chain of distinct consumer-side breakages**, each because a nested element is
now a pointer that the consumer must normalize:
- `github_3238` (`pascal(1)==[[1]]`): **now PASSES** — the `[[1]]` literal
  operand anchors static dispatch. Cheap-equality fixed it.
- `list_depth_test` (`a==b`, both `[[[1]]]`, depth 3): **fails** — both operands
  are *symbols* (no literal to anchor static dispatch) and the level-3 element
  is a LIST behind a materialised symbol → identity-or-nondet → nondet. Needs
  either symbol→literal resolution (via `list_literals`) or precise symbolic
  deref (the expensive path).
- `list_extend13/14/16` (`x.extend([1] + r for r in [[]])`): **fail with a
  spurious uncaught exception** at the concat/extend itself — a wrapped `[[]]`
  element flowing through `[1] + r` and the generator-extend, NOT an
  equality-cost issue. A separate consumer fix (concat/iteration must
  deref/normalise the wrapped element).
- the **crash** the original byref sweep surfaced — not yet re-triaged.

**Architectural assessment.** byref-everywhere requires EVERY consumer of a
nested element (concat, extend, iteration, equality, depth/`repr`, `in`,
dict/set subscript, …) to normalise the wrapped pointer. There is an inherent
conflict: *aliasing* needs the pointer preserved through reads/copies, while
*structural ops* need it dereferenced to a value — you cannot have both
transparently, so each consumer needs explicit handling. That is a large,
pervasive substrate, and the false proof it fixes is **corpus-invisible**.
Recommendation stands: keep the cheap-equality win (it is sound + standalone),
and for the actual soundness bug prefer the proportionate targeted guard below;
only pursue full byref as a deliberate, large, separately-scoped project. The
"static-dispatch where the tag is known, sound bounded fallback otherwise"
pattern is the right tool for each consumer *if* that project is undertaken.

**(Earlier note, now superseded for equality.)** By-value nested lists get deep
structural `==` for free from a single `equal_exprt`; the static-dispatch helper
matches that cost for the literal case and degrades gracefully (sound nondet)
for the symbolic case. (A whole-program heap model — [§5](#dict-byref) — would
*not* avoid this cost; it would universalize per-element dynamic-tag dispatch
and regress the cheap flat-scalar path, so it remains off the table.)


**Proportionate sound fix — LANDED 2026-06-17.** Option (a), the taint +
element-mutation guard, keeping the by-value representation (so the cheap
structural ops are untouched). A list symbol is tainted when it is assigned the
result of a replicating/sharing op (`l*n` / `n*l`, `a+b`, `a[:]`, `a.copy()`,
`list(a)`) whose result element type is a by-value mutable container; BOTH the
target and any Name operand are tainted (a shallow copy shares elements with
its source), and the taint propagates on a whole-list alias `h = g`. A
`python-model-bound` report+cut (`assert false` + `assume false`) is emitted
when an *element* of a tainted list is mutated in place — subscript-assign
`g[i][j] = v` (in `convert_assign`) or a mutating method `g[i].append(..)`
(`{append,extend,insert,remove,pop,clear,sort,reverse}`, guarded in
`convert_expr_stmt`, since nested-element method calls bypass `convert_call` /
`try_method_call`). Read-only access, whole-slot reassignment (`g[i] = v`), and
`g.method(..)` on the outer list stay precise; distinct nested literals and
comprehension rows are never tainted. Helper `is_aliased_list_element` +
`emit_aliased_mutation_guard`; taint set `aliased_mutable_lists`. Validated: the
five confirmed false proofs (repetition / concat / slice / `list()` / method)
now report; read-only / nested-method-write-back / distinct / outer-append /
flat-scalar stay SUCCESSFUL; native crash-scan 0/589; `regression/python` green
(+`nested-list-alias-modelbound`, +`nested-list-alias-readonly`); ESBMC sweep
PASS 2933, **0 regressions**.

**Documented residual (sound-but-incomplete, accepted tradeoff).** The guard
covers direct element mutation of a tainted list, its whole-list aliases, AND
direct element-extraction (`row = g[i]; row.append(..)` / `row[j]=..` — the
extracted name is tracked as a shared inner, `8795ef2408`). It does NOT cover:
an alias of an extracted inner that takes the escaped_mutables early-return path
(`row=g[i]; s=row; s.append(..)`), the append-of-element producer
(`a.append(a[0])`), and function-parameter / container-stored aliases — these
can still false-prove via the same aliasing. Closing them fully needs the
by-reference substrate (above), which is empirically untenable /
disproportionate. (Option (b), a coarser construction-time report, was
rejected: it cuts common read-only slice/copy.) **Do NOT pursue the byref
substrate.**



- **Method default-args not filled on optional/union-typed receivers
  {#method-default-optional} — RESOLVED (2026-06-15, `27d677fc91`).** A method
  called on a value whose static type is optional/union (e.g. the
  `Match | None` returned by `re.search`, represented as `python_value`)
  dispatched through the tagged-union path, which built the call from provided
  arguments only; omitted defaults were nondet-filled by the GOTO layer,
  making any default-driven branch nondet. Fixed by filling trailing parameter
  defaults in the `python_value` method dispatch (single- and multi-owner
  branches) from the module-independent `default_values` map that
  `convert_user_call` also uses — one fix for every defaulted method reached
  through an optional return. It made `re.search(...).group()` (no-arg) as
  precise as `group(0)`. Sound: required (default-less) args still left to the
  GOTO layer; provided args override defaults.
- **Empty-container element type defaults to `int` {#empty-container-elem-type}
  — RESOLVED (front-end annotation half 2026-06-15 `9173fea6ce`; back-end
  defensive net 2026-06-15 `1c62587a8e`).** An empty list literal `result = []`
  is given a `python_int` element type. Appending a non-int (e.g. a string)
  then emits an element-type coercion; under `--python-smt-strings` a string
  element produces an `smt_string -> signedbv` typecast that hit
  `PRECONDITION(false)` in `smt2_convt::convert_typecast` — a hard abort.
  - **(a) front-end — DONE.** `x: list[T] = []` now re-types the empty literal
    from the (authoritative) annotation in `convert_ann_assign`, so
    `parts: list[str] = []; parts.append(s)` is precise (and `dict[K,V] = {}`
    already pinned its types).
  - **(b) back-end (defensive) — DONE (`1c62587a8e`).** `find_symbols`
    pre-declares a fresh nondet (`string_cast.N`) of the destination sort and
    `convert_typecast` emits it for an otherwise-unconvertible
    `smt_string`↔scalar cast, instead of `PRECONDITION(false)`. The
    non-annotated `result = []` + call-indexed slice append in a loop no longer
    aborts — the crash class is fully removed (sound nondet on the unconvertible
    cast).
  Resolving (a) cleared blocker #1 for precise `re.findall`/`re.split`
  ([regex-position-plan](python-frontend-regex-position-plan.md) Phase 2).
- **String operations:** the `string-concat` loop cluster
  (`string-concat4/5/6/13`) and `string.digits` / `string.ascii_uppercase`
  population are **all PASS now** — closed. No open items in this group.
- **ESBMC-nondet primitives (PARTIAL):** most of the previously-listed
  residuals are **closed** (`nondet_list17/18`, `nondet_dict14` now PASS).
  Still open (DIFF): `nondet_list4` (a typed-int nondet element can take
  the None sentinel; excluding it would be an under-approximation) and
  `nondet_list5` (loop-unwinding sensitivity).
- **TIMEOUT tests — re-profiled 2026-06-15 (frontend `--program-only` vs full,
  unwind 10); no single root, now grouped by cause:**
  - **Stale (close):** `github_3560_1/3/4` now verify FAILED in < 0.2 s — no
    longer timeouts; drop from the list.
  - **Frontend/`python_value`-SSA-bound** (the `--program-only` conversion
    itself times out): `dict65` (heavy dict `.items()/.keys()/.values()` +
    `sorted`-of-tuples + tagged-union compares), `shedskin` (breadth of
    list/dict ops; frontend ~9 s), `github_3684` (frontend ~19 s). These are
    driven by the **§8 `python_value` SSA expansion** target (cap
    `field_sensitivity` depth / struct-level SSA for tagged unions) — fixing
    that should clear them.
  - **Frontend, structural** (own root): `list31` (deeply nested list literals
    `[[[]]]`, lists of dicts/sets — nested-literal construction / element-type
    prescan recursion) and `github_3626-nondet`
    (`[[nondet_int()] for _ in range(2)]` then `nested[i].pop()`/`len` with a
    symbolic index — symbolic-index-into-nested × bounded-unroll). Each needs a
    targeted profile (`scripts/profile_cbmc.py`) to pin the hot function.
  - **Solver-bound** (frontend < 0.1 s, full times out): `nondet_list6`,
    `redundancy` — formula structure / SAT-SMT, a separate (smaller) group from
    the frontend timeouts; needs formula-size / solver-tuning analysis.
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

**Guard-coverage audit + extension (2026-06-18).** A soundness audit of
the taint guard's *actual* coverage found it had only ever fired on the
**replication** channels (repetition `*`, concat `+`, slice `[:]`,
`copy`/`list`); three further channels that also alias an extracted
mutable inner were **unguarded false proofs** (corpus-invisible; sweep
neutral). Two are now guarded (sound over-approximation, sweep PASS 2945
= baseline, 0 regressions):
- **Self-append / insert** `a.append(a[i])` / `a.insert(_, g[i])` — taint
  the receiver when the appended value is a subscript or a shared-inner
  name (a scalar subscript is harmless: no nested mutation follows).
- **New-container literal** `h = [g[i]]` — taint the target when a list
  literal holds a subscript / shared-inner element.

**Residual (documented, NOT yet guarded): extraction from an *untainted*
list** — `r = g[i]` where `g` is a plain nested literal (no prior share
op), then mutate `r` and observe via `g[i]`. This is the matrix-row
pattern (`row = m[i]; row[j] = ...`), which is **common and correct** when
the extracted row is used on its own; tainting every such extraction
would over-report and regress real numeric code. Distinguishing the
unsound (aliasing-observed) sub-case from the sound (row-only) sub-case
needs data-flow, and the precise representational fix is the
byref-at-construction substrate that is empirically untenable (above).
So this single channel stays a known, corpus-invisible residual false
proof. (The doc previously overstated the guard as covering all unsound
patterns; this audit corrects that.)

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

**Instance-`__dict__` substrate — PLANNED (2026-06-15 spike).** Today an
instance is a fixed CBMC struct whose fields are the attributes discovered by
scanning `__init__`/the class body (`member_exprt{obj, attr,
st.get_component(attr).type()}` throughout `python_converter_assign.cpp`), so
there is nowhere to store a *dynamic* attribute, a shadowing instance entry, or
a stateful descriptor's per-instance state. The fix is a per-instance
**`__dict__`** (a string-keyed `python` dict `[str, value]`, now efficient
after the §5 inlining) plus routing attribute access through the CPython
lookup order (PLR §3.3.2 descriptor protocol + §3.2):

1. data descriptor (defines `__set__`/`__delete__`) found on `type(obj).__mro__`
   → `descr.__get__` / `descr.__set__`;
2. `obj.__dict__[attr]` if present;
3. non-data descriptor (`__get__` only) or plain class attribute on the MRO;
4. `__getattr__` fallback, else `AttributeError`.

**Hybrid (perf):** keep the fixed struct fields for *declared/annotated*
attributes (the current fast path) and add the `__dict__` only for the dynamic
remainder, so common code is unaffected. **Phases:**

1. **Instance `__dict__` for dynamic attributes** — a write to an attribute not
   in the struct stores into `__dict__`; a read of one reads from `__dict__`
   then `__getattr__`. Fixes dynamic attribute assignment. **PARTIAL via static
   discovery (2026-06-17, `9f52de49ee`):** instead of a runtime `__dict__`, a
   whole-program pre-pass discovers `<x>.attr = ...` for `x` a typed parameter
   (pre-existing) OR a local bound to an instance (`c = C()` / `c: C`, new) and
   declares `attr` as a struct field up front. Covers the common literal-name
   case with no per-instance storage cost; the **runtime `__dict__` is still
   needed** for truly-dynamic names (`setattr(o, computed, v)`), instances
   returned from functions / aliased through containers, and method shadowing
   (next).
2. **Shadowing** — route reads through step (2) before the class lookup, so an
   instance `__dict__` entry shadows a class non-data attribute (method). Fixes
   `method-shadow-knownbug`. (The static-discovery phase deliberately *skips*
   method-named attrs, so it does not regress dispatch.)
3. **Custom data descriptors** — `__set__`/stateful `__get__` via class-object
   descriptor instances whose storage is the instance `__dict__`. Largest step
   (needs class objects carrying descriptor instances).

**Risk:** the attribute-access reroute is pervasive; mitigated by the hybrid
fast path. This substrate also subsumes `setattr`/`getattr` with dynamic names.

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

**Status: PARTIAL** (reclassified 2026-06-15 after a spike — the prior "no
plan yet" was stale). The **sound concurrency-collapsing design is already
chosen and partly implemented**: `await EXPR` is lowered to `EXPR`
(`python_converter.cpp:4725`, sequential / no concurrency), `async def` is
registered and called like a regular function (`defs.cpp:1284/2212`), and a
real `asyncio` stub (`src/python/library/asyncio/__init__.py`) collapses the
event loop (`run`/`run_until_complete` pass the eagerly-evaluated coroutine
value through, the loop is a no-op). Verified working: `assert (await f(4))
== 5` *inline* is SUCCESSFUL — `await` evaluates the coroutine to its return
value.

**Live gap (specific):** binding an `await` / async-call result to a variable
leaves the target **unbound** — `v = await f(4); assert v == 5` raises
`UnboundLocalError` (and so does `v = f(4)` for an `async def f`). The inline
form works, the assignment form does not, so the async-call result isn't
captured at the assignment chokepoint (likely a `side_effect_expr_function_callt`
handled in expression context but not in the statement-level assign, or an
async call returning a coroutine placeholder at statement level).

**Plan.**

1. **Fix `await`/async-call result binding** (the live bug) — make `v = await
   coro(...)` and `v = coro(...)` bind the coroutine's return value at the
   assignment path, mirroring a regular call. Then drive `asyncio.run` /
   `run_until_complete` to actually call the coroutine. Single-task `await`
   chains are then **sound and exact**.
2. **Async generators** — reuse the existing generator **list-with-cursor**
   lowering ([§1](#generators)); `async for` / `async with` desugar to the
   sync `for` / `with` over the collapsed awaitable.
3. **Soundness boundary (documented).** Concurrent tasks
   (`asyncio.gather` / `create_task`) with *shared mutable state* execute as
   **one sequential schedule**, so inter-task interleavings at `await` points
   are **not** explored — a concurrency-bug unsoundness. This is **out of
   scope** (faithful interleaving = a full concurrency model) **unless** the
   tasks share no mutable state (then any schedule is equivalent and the
   sequential model is sound). PLR §8.8.x.

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
