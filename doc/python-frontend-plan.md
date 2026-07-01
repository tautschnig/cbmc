# CBMC Python frontend — plans & future work

# Python frontend plan (everything except strings)

This is **the** forward-looking backlog for the Python frontend
(`src/python/`), covering everything **except** `str`/`bytes`/`re`, which
has its own dedicated
[strings & regex plan](python-frontend-strings-plan.md). It is the
companion to
[python-frontend-architecture.md](python-frontend-architecture.md): every
gap or PLR deviation noted there links to a section here (or in the
strings plan). Each section is either a **concrete plan** (with a fix
shape and scope) or an explicit **no plan yet**.

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
> 3. **Native SMT-LIB String backend** ([§3](python-frontend-strings-plan.md#strings)) — the strategic
>    precision target; the refined-string frontier is measured as largely
>    tapped, so this is the comprehensive answer (also unblocks the
>    `github_3090` per-execution-content spike + JBMC native `smt_string`).
>    Sustained, phased effort; refined parity is the constraint.
> 4. **Breadth / capability** — module breadth ([§6](#modules), by corpus
>    import frequency); async result-binding ([§13](#async), small live bug);
>    icontract multi-level Liskov + C3 MRO ([§11](#icontract)).
> 5. **Deferred** — `python_value` field-by-field SSA (perf-only, [§8](#performance));
>    closure Phases 4–6 (~0 corpus); regex finishers ([§4](python-frontend-strings-plan.md#regex), fold into
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
> - **Residuals (investigated 2026-06-18).**
>   - *Mutation-collector whole-program coverage* — `collect_function_global_
>     mutations` now also scans **imported-module** function bodies (called
>     from `process_imported_module`), not just main, so the mutated-globals
>     summary covers the whole program. Sound, sweep-neutral.
>   - *R1 — cross-module global-dict mutation (deeper, NOT fixed; documented
>     false proof).* `from modx import state, mutate; mutate(); assert "b" not
>     in state` still verifies wrongly. Root is NOT the collector (now fixed)
>     but two deeper cross-module layers: (a) the imported module's global dict
>     isn't pre-typed for *its own* functions (the same Pass-0 dict-pre-typing
>     gap, but Pass 0 scans only main), so `mutate`'s `state["b"]=2` is dropped
>     (empty body); (b) cross-module global *binding* is broken — main shows
>     "Unknown variable: state" for the imported global. A focused cross-module
>     effort (imported-module global pre-typing + import-binding) — separate
>     from the (now-sound) intra-module global-mutation machinery.
>   - *R2 — `**d` unpack of a mutated global dict (pre-existing precision gap,
>     false positive).* `f(**d)` reads the conversion-time `dict_literals`
>     literal; after a real mutation the literal is correctly invalidated, so a
>     later `f(**d)` can't fold the unpack (it failed before this work too — the
>     unpack never reflected runtime mutations). Sound (spurious failure, not a
>     false proof), rare. The real fix is `**d` unpacking from the *runtime*
>     dict (bounded key scan) rather than the conversion-time literal.
> - **P0/P1 pass (2026-06-19).**
>   - *R1 cross-module global-dict mutation — FIXED (`process_imported_module`
>     now registers Dict/List-literal module globals, not just Constants), so
>     `from m import state, mutate; mutate()` then a read of `state` is sound.*
>   - *Sweep re-scan: no new false proofs* (the `SUCCESSFUL`-where-`FAILED`
>     DIFFs are a subset of the original 21, all triaged).
>   - *Call-signature false-proof whole-group — FIXED.* Signature validation
>     was fragmented (constructors/methods used `validate_call_signature`
>     [too-many + unknown-kwarg only]; free functions had a partial inline
>     check) and missed missing-required-positional, multiple-values, and
>     missing-required-keyword-only — a cluster of false proofs
>     (`github_3010_*`, `github_3015_*`: CPython raises `TypeError`/`SyntaxError`,
>     we verified). Unified: `function_required_positional` +
>     `function_required_kwonly` (free fns + methods); the missing/multiple/
>     kwonly checks added to `validate_call_signature`; free-function calls
>     routed through it (bound-method `m=obj.f; m()` excused via leading-`self`
>     detection). ~9 false proofs eliminated; 0 regressions. **Note:** these
>     `_fail` tests expect ESBMC's *exact* `TypeError:`/`SyntaxError:` message,
>     so the sweep verdict stays DIFF (our generic uncaught-`TypeError` FAILED
>     doesn't match the string) — the soundness win is not reflected in PASS.
>     Residual: `*args`+required-kwonly combos (vararg guard skips kwonly);
>     `github_3560_1`-style `split` precision (list-valued, hard) unchanged.

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
>   byte-operated structs — [#native-byte-ops](python-frontend-strings-plan.md#native-byte-ops); lower.)
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
>   ([§4](python-frontend-strings-plan.md#regex)); **re.findall / re.split stay a sound over-approximation on
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
  ([#native-byte-ops](python-frontend-strings-plan.md#native-byte-ops)): the dict by-reference *mutation* crash
  is **RESOLVED** (the Any-container promote+write-back routes the dict through
  a clean typed view, no byte op); only a latent general `smt_string`-in-byte-op
  gap remains with no corpus instance.

**P2 — Back-end capability parity (two tracks, both first-class).**
- *Refined track — default back-end toward SMT parity (kept, not downgraded):*
  - constant-pattern **regex precision under refinement strings** — the proper
    fix for the `re*` benchmarks now nondet under the default back-end
    (string-refinement regex axioms; [§4](python-frontend-strings-plan.md#regex) "Wave 3", research-grade);
  - **membership convergence** (`not_contains` existential-witness
    instantiation) and **lexicographic ordering**;
  - **producing-op precision** (slice / `replace` / `repeat`) under refinement.
- *SMT track — native reach ([§3](python-frontend-strings-plan.md#strings)):*
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
      loops; same shape as the list-valued split loop ([§4](python-frontend-strings-plan.md#regex)), but
      perf-heavy on fully-symbolic subjects, so gate/measure before enabling.
    - `split` (list-valued) and `repeat` with symbolic `n` (nonlinear) — see
      the list-valued plan ([§4](python-frontend-strings-plan.md#regex)) and keep `repeat`-symbolic nondet.

**P3 — Regex reach (SMT path; [§4](python-frontend-strings-plan.md#regex)).**
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

> **Current state (2026-06-30).** The differential-oracle baseline is **3** known
> false proofs + **4** intrinsic/out-of-subset residuals (down from 22 over the
> 2026-06-28→30 arc). The canonical, categorised list lives in the
> [architecture doc master inventory](python-frontend-architecture.md) — see its
> **CURRENT STATE (2026-06-30)** header; this section is the chronological design
> log.
> **No known false proofs remain** in the differential oracle (default config,
> external CPython-semantics corpus). *Closed 2026-06-30:* `gen_send_before_start`
> (commit `70401b6d90`, §1 Phase 1 OUTCOME — cursor-encoded priming + a `.send()`
> handler + whole-group expression-context yield counting) and the two
> decorator-application false proofs `dec_not_callable` + `dec_wrong_arity`
> (commit `27bb327b26`, §15 OUTCOME). A genuine generator-object identity model
> (aliasing / container / `for`-after-`next`) remains future work and **IS a
> known false-proof cluster** (consumption-state; pinned 2026-06-30 via a
> proactive sweep — `gen-foriter-after-next`/`gen-alias-consume`/`gen-in-container-consume`
> knownbugs; outside the oracle corpus).
> The **4 intrinsic residuals** (`ORACLE-INTRINSIC`): the annotation-laundering
> family — `004` (arg), `007` (list-element/append), `ty-010` (return-annotation)
> — caught under opt-in `--python-check-annotations`; and `d1` (int→float at a
> call boundary, out of the PyHard subset).
> Closed over the arc (each whole-group, validation-gated, CORE lock-in): the
> reference-semantics-for-instances instance-identity cluster; binop eval-order
> (forward AND mirror); `@property` setters; shift/bitwise + both-union operand
> obligations; tuple-unpack arity; method mutable-default sharing;
> `dict.get`/`pop`/`setdefault` default type; fixed-tuple slicing;
> `del`+`__getattr__` retype; the **dunder-protocol-missing** group (9 sites);
> **format-spec** validation (3 sites); **comparison/ordering** (missing-dunder +
> mixed-category); **hashability** (dict key / set element); **@dataclass**
> construction (arg→field binding, also −59 false alarms); **dunder-return
> contracts** (`__len__`/`__str__` type, `__len__` negative value); the
> **builtin-edge** family (divmod/ord/round/sum/join/int(inf|nan)/encode/iterate-
> scalar); and the soundness corner-cases (`set().pop()` masking-assume).
> The dated entries below are retained for design rationale.

**Standalone PLR re-audit (2026-06-26).** A differential pass (~255 generated
probes under CPython vs cbmc) fixed one correctness false proof and pinned five
new narrow ones as KNOWNBUG regression tests:
- **FIXED — float floor division `//`** computed as true division (no floor:
  `7.0 // 2.0` proved `==3.5`); now floored (`float-floordiv-correct`, CORE).
- **FIXED (2026-06-26) — extraction-then-mutate via annotation**: `r: list =
  c[i]; r.append(x)` now records `extracted_container_alias` on the AnnAssign
  path too, so the havoc-on-mutation guard fires (`annotation-extract-mutate-guarded`, CORE).
- **FIXED (2026-06-26) — method-call argument tag obligation**: the main method
  param-binding now routes through `coerce_call_argument` (not raw
  `safe_typecast`), so the provenance-gated TypeError obligation fires at method
  calls too (`method-arg-tag-obligation`, CORE).
- **OPEN** (each a KNOWNBUG; the fix sketch in parentheses):
  - `float-bitwise-typeerror-knownbug` / `sequence-mul-float-typeerror-knownbug`
- **FIXED (2026-06-26) — non-numeric operand TypeErrors (P1.1, whole-group)**: a
  concrete float operand to `& | ^ << >> ~`, and `seq * float` repetition, now
  raise TypeError via a unified operand-type obligation in convert_bin_op /
  convert_unary_op (concrete-float-only, so set bitwise is unaffected).
  `float-bitwise-typeerror`, `sequence-mul-float-typeerror` (CORE).
- **FIXED (2026-06-26) — positional-only param passed by keyword**: recorded
  per-function (`function_posonly_params`) and flagged in the shared
  `validate_call_signature` (a `**kwargs` callee correctly absorbs the keyword,
  no FP). `positional-only-kwarg-typeerror` (CORE). **All five re-audit findings
  are now closed**; the remaining open false proof is the per-instance-identity /
  aliasing cluster (see the P2 note above).

**Narrowing-invalidation cluster sweep (2026-06-28).** Swept the cluster for
cheap sound-guard closures (like the `__setattr__`/`__getattribute__` over-approx).
Findings:
- **CLOSED — enum `.value` after a member retag** (`enum-value-after-mutation`
  now CORE): two coordinated fixes — a HETEROGENEOUS enum's value type is now
  `python_value` (union), and `.value` resolves on an enum-typed **variable**
  (`enum_member_vars`) *and* **field** (`enum_typed_fields`) to the stored member
  value. A retagged member used at the wrong type now routes through the
  operand/tag obligations. Also fixed the common precision gap (`c = C.R;
  c.value`). Tests `enum-var-value-precision`, `enum-var-retag-typeerror`.
- **NOT a cheap guard — context-manager `__enter__`/`__exit__` field mutation is
  the SAME root as composition/instance aliasing**: a class instance bound to a
  CONCRETE-class-typed param/field (`t: SomeClass`) is value-COPIED (identity
  lost), so a mutation through the holder is invisible to the original (verified:
  even a plain int mutation is lost). The Any-typed path preserves identity
  by-address. So `context-manager-enter-mutation-knownbug` unifies with
  `instance-aliasing-knownbug` / `shared-object-aliasing-knownbug` — all close
  together with a reference-semantics-for-instances effort (the high-value
  whole-group play), not a per-feature guard.

**Reference-semantics-for-instances PLAN (2026-06-28).** *(COMPLETED 2026-06-28 —
Phases 1+2+3 landed; all five cluster pins are now CORE [`instance-aliasing`,
`instance-field-aliasing`, `shared-object-aliasing`, `instance-return-aliasing`,
`context-manager-enter-mutation`]; oracle baseline dropped 22→17. See the
COMPLETE [instance ref-semantics plan](python-frontend-instance-reference-semantics-plan.md).
The "now pinned as KNOWNBUG … to flip to CORE" wording below is the design-time
state.)* The cluster above is now
designed and phased in
[python-frontend-instance-reference-semantics-plan.md](python-frontend-instance-reference-semantics-plan.md):
instances are by-value at the three remaining copy sites (local assignment,
field store, return) while params/`self` are ALREADY by-reference pointers and
the Any path is by-address. The fix extends pointer-reference representation to
those sites (phased: return → local-alias → field/composition, each
validation-gated). Pinning suite landed: CORE guards + KNOWNBUG acceptance
criteria. Perf gate per phase: single-level instance pointers are already
default+fast; deep-composition `==` is the risk to measure (the ref_mutables
cliff).

**P2 composition-aliasing investigation (2026-06-26).** Surfaced a *simpler*
sibling of the composition false proof: **direct instance aliasing** `b = a;
b.x = 99; read a.x` (cbmc value-copies the instance at a top-level `b = a`
assignment) — pinned `instance-aliasing-knownbug` (+ oracle corpus). The natural
lead — extend the list/dict `alias_targets` pointer mechanism to class instances
— was spiked and **does not work as a one-line change**: instance attribute
read/write does not route through the alias pointer the way list subscript does
(the per-instance-identity / attribute-field-reference problem the ref-semantics
spike flagged as not-viable-as-default). **The viable sound fix is a
havoc-on-mutation guard** (the same shape as the extraction-then-mutate guard:
record `b = a` instance aliases; on a mutation through either alias — attribute
assign or a mutating method call — havoc the other so a later read is nondet,
sound over-approximation). This closes the `b = a` case without true reference
semantics; the composition-via-constructor-parameter case
(`shared-object-aliasing-knownbug`) needs the same guard generalised to
aliasing through parameters/attributes. Deferred to a focused effort (the guard
must avoid over-havocing common instance-passing patterns — validation-heavy).

**Update (2026-06-26, second round).** Re-attempted the *precise* path more
thoroughly (promote `b = a` instance to a pointer in convert_assign AND deref
alias-promoted instance pointers in convert_name, gated on `alias_targets` so
`self` is untouched). It STILL did not propagate: the goto shows `b := a` is a
plain struct **copy** emitted by a symbol-creation path *upstream* of the
`alias_targets` block, so neither the promotion nor the deref fires. Conclusion:
precise instance aliasing is a genuine **reference-semantics project** (instances
must be heap/pointer objects, like the container ref-semantics that is opt-in
only), not a point fix. The havoc-guard remains the only contained *sound* option
but carries a real precision cost on the **common** `obj2 = obj1; obj2.mutate();
obj1.use()` pattern (reading shared state through the other alias). Both options
are tradeoffs, not quick fixes — so the false proof stays pinned
(`instance-aliasing-knownbug`, oracle baseline) pending a dedicated reference-
semantics-for-instances effort or an accepted havoc-guard precision tradeoff.

**P1 tractability finding (2026-06-25, evening).** Investigated the two
candidate "tractable" false proofs; neither has a clean DEFAULT-mode fix:
- **The ty Any-laundering cluster (004/005/007) shares ONE root** — a wrong-typed
  value crosses a declared-type boundary (call param, `list[int]` element via
  `append`, dict value) and is coerced to the declared type, *losing* its actual
  type, so a later use as the declared type does not fault (the concrete
  `"s" + 1` IS detected directly — the value just is not typed str at the use).
  Catching this by DEFAULT is precision-bounded: storing/passing a mismatched
  value is legal Python (the error only arises on a *use* as the wrong type), so
  a default obligation false-alarms on code that never misuses it (the
  `greet(42): pass` lesson). The correct home is opt-in
  `--python-check-annotations`. **DONE (2026-06-29):** the flag now catches the
  argument boundary (004), the `list.append`/`insert` element boundary INCLUDING
  a call arg `xs.append(src())` (007, via the callee's static return type -- no
  double-eval), and the `dict[K,V]` value-store boundary (`d[k] = v`). All
  opt-in, default UNCHANGED (0 sweep regressions), runtime-tag/concrete-type
  guarded (Any element/value types and genuinely-matching values never
  false-alarm). **Remaining:** `005` is NOT an annotation case (no annotation) --
  it is `{}.get("k", "s")` returning the dict's inferred concrete value type
  (int) instead of `value_type | type(default)`, a separate DEFAULT-mode
  `.get`-return-union precision fix. **DONE (2026-06-29):** `dict.get`/`dict.pop`
  `(k, default)` now return `value_type | type(default)` -- a provably-absent key
  (empty/constant-miss) returns the default in its OWN type, a present constant
  key returns the value, and the symbolic case returns a python_value union. 005
  resolved in DEFAULT mode (oracle 12 -> 11), 0 sweep regressions. `setdefault`
  (which also INSERTS the default) was the remaining group member: **DONE
  (2026-06-29)** -- the empty-dict-value prescan now infers the value type from a
  setdefault default in an assignment (`v = a.setdefault(k, d)`), so the dict
  value type accommodates type(default); the default is stored AND returned in
  its own type, consistently (the return and a later d[k] read agree). The whole
  get/pop/setdefault default-fallback group is now closed (the heterogeneous
  same-dict case -- int values AND a str default -- stays the pre-existing
  first-use-wins inference limitation). And making the (now-complete)
  check-annotations flag default-on
  is the deferred (B) policy decision (the benign-false-positive tradeoff).
- **Default-on MEASURED + DECLINED (2026-06-29).** Ran the full sweep with
  `--python-check-annotations` forced on: **34 regressions / 2718 (~1.25%)**,
  almost all the IRREDUCIBLE benign class (CPython does not enforce annotations
  and the value is used per its ACTUAL type, so CPython never raises -- e.g.
  `x: int = b.f()` where `f()->str` then `assert x=="Woof!"`; `div: int = 1/count`
  true-division float). A boundary obligation cannot distinguish "misused (real
  bug, sound to catch)" from "used per actual type (benign)" without use-site
  flow analysis. Default-on therefore declined; opt-in remains the home.
- **Opt-in precision improved (2026-06-29), FP 34 -> 27.** Of the 34, ~7 were
  CHECK imprecisions (not benign): (1) a `python_value` union component (the
  container ABCs Sequence/Iterable/Mapping model as Any) now satisfies the union,
  so a list arg to `Sequence[str] | None` is accepted (a list IS a Sequence;
  sequence_2/3/4); (2) the list-append + dict-value-store checks now gate on
  EXPLICIT container annotation provenance (variable_annotations), so an inferred
  empty `[]`/`{}` (e.g. `setdefault(1, [])`) is not flagged (dict_setdefault_list).
  Both PLR-safe (suppress-only). The remaining 27 are the irreducible benign class.
- **Tuple-unpack arity — FIXED (2026-06-29).** `a, b = (1,)` (and ty-015`s
  `a, b = make()` returning the fixed `(1,)`) silently skipped the missing `_i`
  field; a fixed-arity mismatch is now a definite ValueError (gated on no Starred
  target). Oracle ty-015 resolved. (Residual: starred unpack from a TUPLE literal
  rhs `first, *rest = (1,2,3,4)` is a pre-existing spurious FAILED -- the starred
  path only expands a LIST rhs; separate, not a regression.)
- **Both-union strictly-numeric binop — FIXED (2026-06-29).** `p.a - p.b` where
  BOTH operands are tagged unions retagged to str now emits the operand-type
  TypeError obligation (Sub/Div/FloorDiv/Pow only -- Add/Mod/bitwise excluded:
  concat / str %-format / set ops are valid). Oracle a12 resolved. Same
  obligation block as the b3/b5 shift fix (one coherent operand-type group).
- **`a10` mutable-default shared state in a METHOD — FIXED (2026-06-29).** A
  method`s mutable (list/dict/set) default is now frozen into a static-lifetime
  symbol with a once-only module-init (queued in convert_class_def, guarded per
  class::method::index, flushed at the start of convert_module_body) -- so it is
  evaluated once and SHARED across calls and instances (PLR §8.7). Annotated
  method defaults accumulate precisely (call1==1, call2==2, ...). Oracle a10
  resolved. (UNannotated method defaults bind through a separate Any path and do
  not yet accumulate -- pre-existing limitation, not a regression.)
- **`c2` TypedDict del through a call — DEEP, OPEN.** `del p["x"]` inside
  `rm(p)` does not propagate the deletion to the caller`s dict, so a later
  `pt["x"]` misses the KeyError. Root: dicts are shared by-reference for
  EXISTING-key value modifications (`p[k]=v` propagates) but NOT for STRUCTURAL
  mutations -- adding a new key (`p["z"]=9`) or deleting one (`del p["x"]`)
  through a call does not cross the boundary. This is the documented partial
  dict-by-reference barrier (length/keys arrays not shared across the call), the
  same representation limit as the dict-value-byref work. Deferred.
- **`ty-010` slice of `tuple[int, ...]` then a str method — DEEP, OPEN.**
  `tuple[int, ...]` is modelled as `python_value` (Any), so `t[1:]` is Any and a
  `-> str` return annotation is trusted, so `f(...).upper()` is accepted. Needs a
  real variable-length-tuple model (symbolic length + element type) so the slice
  is a tuple, not Any. Deferred. **Adjacent gap — FIXED (2026-06-29):** a str-only
  method (`upper`/`lower`/...) on a CONCRETE non-str built-in receiver
  (`(5).upper()`, `[1,2].upper()`, `(1,2,3).upper()`, `{}.strip()`) now raises
  AttributeError. Gated to avoid FPs (only str-only names -- not count/index;
  not Any/str/bytes=list[uint8]; not user-class instances). 0 sweep regressions.
  This closes the CONCRETE-receiver class; ty-010 itself (the Any-erasure variant
  via `tuple[int, ...]`) still needs the var-tuple model.
- **`a17` same-expression eval-order × union-retag** (`e.gm() + e.x`) is a niche
  union-tag sequencing subtlety (the int-typed version is already correct;
  swapped operands `e.x + e.gm()` already agree with CPython) — not a localized
  fix. Left as KNOWNBUG.

**Union-return-path precision bug — FIXED (gated widen).** A function
with a concrete return annotation (`-> int`) and a return path that yields a
union/Any value (`return x` where `x: int|str`) puns that `python_value` into
the concrete int return slot, corrupting the value read on the *other* (taken)
path too -- `def g(x:"int|str")->int: if isinstance(x,str): return len(x); return x`
mis-evaluates `g("abc")` (returns the wrong value, a SOUND spurious failure).
Root: with an annotation, `infer_return_type_from_body` is skipped and the slot
is the concrete annotated type; the no-annotation path (which widens to
python_value) is correct. **Fix shape:** widen the slot to python_value when a
return path genuinely yields one -- but the naive `is_python_value_type(inf.type)`
gate over-fires on **forward/recursive calls** (mutual recursion `-> bool`
regressed: the inferer returns python_value from *uncertainty* about an
unresolved call, not a genuine union). A safe fix must distinguish a genuine
python_value-VALUE return from inferer uncertainty (e.g. flag a `return <union
param>` specifically, excluding `return <call>`). Sound either way (precision only). **Fixed:** added
`inferred_returnt::saw_python_value_return`, set only at a genuine
`return <union/Any param>` site (NOT the call-uncertainty path), and widen the
annotated slot to python_value only when that flag is set -- so `g("abc")` is
precise and mutual recursion (`forward-declaration4`) is not regressed. Guard:
`union-return-path-precise`.

**P4 enum `.value` finding (a13).** cbmc does not track enum-member
**reassignment** for `.value`: after `j.s = S.B`, `j.s.value` does not read
`S.B.value` (`j.flip(); assert j.s.value == 2` FAILS -- a precision miss; the a13
false proof, `.value` used as int after a flip to a str-valued member, is the
soundness face of the same gap). Refined (2026-06-26): enum members are represented **as their value** (`j.s == 2`
and `j.s == S.B` both hold after `j.s = S.B`; field reassignment IS tracked, and
constant `S.B.value` works). The ONLY broken case is `.value` on a **runtime**
enum value (`j.s.value`): the `.value` handler matches only the literal
`EnumClass.MEMBER.value` AST shape. Since a member IS its value, the precise fix
is `j.s.value → j.s` -- but it must fire ONLY when the receiver is enum-typed
(`j.s` is stored as a plain int, so `.value` on a non-enum int must stay an
AttributeError), and there is no value-level enum marker nor a (class,field)→
annotation map to detect that. So the fix needs enum-type tracking
infrastructure (a value-level enum tag or a class-field-annotation map) -- a
scoped enum-modelling task, not a clean over-approximation. Left as KNOWNBUG
(a13); the soundness face is `j.s.value - 1` reading a str member value.



**Any/union tag-obligation — status + the call-boundary blocker.** The
arithmetic OPERATOR tag obligation already exists and works (a union/Any operand
with a non-numeric runtime tag raises TypeError on +,-,/,//,%,**; no false alarm
on a genuinely-numeric value or isinstance-guarded code). The SUBSCRIPT
obligation landed (scalar receiver = definite TypeError; python_value receiver
must be a container/CLASS tag). The CALL-BOUNDARY obligation (binding a union/Any
arg to a concretely-typed scalar parameter, which `coerce_call_argument` ->
`unwrap_value` does with no tag check) is now **ENABLED via annotation
provenance**. `explicitly_annotated_params` records the parameter symbol ids
whose type came from a GENUINE source annotation (vs the default Any or a
call-site-INFERRED type); `coerce_call_argument` emits the tag obligation only
for those. This sidesteps the earlier blocker -- lambdas / unannotated params
default to a scalar type yet truly accept Any, so they are excluded
(`keep([A(),B()], lambda e: True)` no longer false-alarms). int/bool and the
int->float numeric tower are accepted. Guard: `param-coercion-typeerror`
(flipped KNOWNBUG->CORE). The provenance set is reusable for return/assign
boundary obligations (future).

**Boundary-obligation scope finding (2026-06-25).** Investigated extending the
provenance-gated tag obligation to the return and assignment boundaries: both are
**already sound** -- a union/Any value flows through `return`/annotated-assign as
a `python_value` (the tag is preserved), so the operator/subscript/call use-site
obligations catch a later misuse; an unannotated return does not false-alarm.
The remaining default-mode gap is a **concrete-type mismatch** at the call
boundary (`use(d["k"])` where `d` is a str-valued dict and `use(y: int)`): the
arg is a concrete `str`, not a `python_value`, so the tag obligation does not
apply. Flagging it by default is **NOT done** because a parameter type-mismatch
is not in general a runtime error -- Python does not check annotations at the
call, only a *use* of the value as the wrong type raises, so a function that
ignores the param (`def greet(name: str): pass; greet(42)`) does NOT raise and
flagging it is a false positive (this regressed 3 corpus tests). It stays covered
by opt-in `--python-check-annotations`. The same precision caveat applies in
principle to the python_value call-boundary obligation (it could spuriously fail
a tag-mismatched union/Any arg bound to a param the function ignores), but that
shape is absent from the corpus (sweep clean); it remains a net soundness win
(removes the param-coercion false proof) with a documented precision caveat.

**Differential triage of the mypy/ty `narrowing-invalidation` cluster: DIVERSE
roots, not one fix.** cbmc does no flow-narrowing, so these manifest as distinct
feature gaps, several already documented / out-of-subset: nested-composition
aliasing (a23 -> the per-instance-identity intractable area), enum `.value`
modelling (a13), context-manager `__enter__` mutation (a8), mutable-default
shared state (a10), inheritance+union virtual dispatch (b3/b5/b6), `__setattr__` /
`__getattribute__` (CLOSED 2026-06-25: reads on instances of classes defining
these intercept-everything dunders are over-approximated to nondet python_value,
routing uses through the tag obligations -- `setattr-override`/`getattribute-override`
are CORE), numeric-tower int-as-float method call (d1,
which the subset marks OUT). Same-expression eval-order x union-retag is the a17
residual. Each is its own modelling project. The distinct roots are now pinned
as public KNOWNBUG regression tests (PLR-correct = VERIFICATION FAILED, to flip
to CORE as each gap closes): `enum-value-after-mutation-knownbug`,
`shared-object-aliasing-knownbug`, `context-manager-enter-mutation-knownbug`,
`setattr-override-knownbug`, `union-use-after-mutation-typeerror-knownbug` — so
the architecture inventory's "known open false proofs" claim is testable. (The
broader set of witnesses is also tracked via the differential harness.)

**FIXED (numeric tower):** `unwrap_value` to a float target now promotes an
INT/BOOL-tagged value's `__int_val` payload to float instead of reading the
unset `__float_val` (was a wrong-value bug for an int-tagged union/Any bound to
a float slot). Guard: `unwrap-int-to-float-promotion`.


**Subscript tag obligation — ADDED (Any/scalar receiver).** Subscripting a value
whose runtime type is not subscriptable now raises TypeError instead of silently
returning nondet/garbage: a concrete scalar receiver (`int`/`float`/`bool`, e.g.
an unannotated param inferred as int then `xs[0]`) is a DEFINITE TypeError
(assert false); a `python_value` (Any/union) receiver must carry a container tag
(STR/LIST/DICT) or CLASS (may define `__getitem__`). Guard:
`any-subscript-typeerror` (flipped KNOWNBUG->CORE). **Still open (KNOWNBUG
`union-use-after-mutation-typeerror`):** the broader tag obligation on
union/Any *extraction* used in an operator (`int|str` read as int via
`unwrap_value`, which reads `__int_val` with no tag assertion) is deferred -- a
blanket assert there is as noisy as the int-overflow blanket guard was (it would
fire on every union/Any read incl. isinstance-guarded branches, because the
frontend adds no flow-narrowing `assume(tag==INT)`). A sound non-noisy fix needs
isinstance narrowing + a definite-mismatch-first obligation; tracked as the
tag-obligation project.


**`del obj.attr` no longer leaves a stale concrete value (FIXED).** For an
instance-only attribute (no `__shadow_` presence flag), `del obj.attr` reset the
slot to the None marker (0 for int), so `c.x = 42; del c.x; assert c.x == 0`
verified even though CPython raises AttributeError. Now the slot is havocked to
nondet (over-approximation), so a post-`del` read cannot be proved equal to any
concrete value. **Properties CLOSED (2026-06-28):** `@property` getters AND
setters are now modeled as data descriptors -- `obj.p = v` dispatches the
`@p.setter` (with side effects) via `emit_property_set`, and the setter no longer
clobbers the getter symbol (each `@p.X` accessor is stored under a distinct
symbol). Closed `a32_property_setter_side_effect` + `b6_property_covariant_override`.
**Residual (open):** the `del obj.attr` + `__getattr__` step-4 fallback is still
not modeled -- after `del`, a concretely-typed field cannot hold `__getattr__`'s
differently-typed return (e.g. `del self.x` then `self.x` returning str from
`__getattr__` while `x` is declared int), so the cross-type `TypeError`
(`c4_getattr_fallback` / laurel-006) is not caught. This needs per-instance
field-presence tracking or making deletable fields `python_value` -- an invasive
fixed-struct-model change. Pinned in the oracle; tracked as the attribute-protocol
gap.


**Sound-mode + numeric/type audit (2026-06-25). One leak found+fixed; rest
clean.**
- **P1 — `--python-unbounded-ints` (the SOUND mode) bitwise leak FIXED
  (`c0b7ae0885`).** `&`/`|`/`^` truncated unbounded operands to signedbv[64]
  and wrapped (`(2**70) & (2**70)` proved == 0). Fixed: arbitrary-precision fold
  for constants; fit-select (64-bit when both fit, else sound nondet) for
  symbolic. Exhaustively re-probed ~, %, //, divmod, abs, comparisons,
  bit_length, bool, float(), int(), * -- all sound; bitwise was the only leak
  (shifts fixed earlier, `b3682b875c`). Guards: `unbounded-bitwise-soundness`,
  `unbounded-bitwise-precise`.
- **P2 — bytes / complex / Decimal: clean.** No false proofs (index/slice/len/
  concat/eq; arith/abs/eq; exact base-10 add); precise on positives. Guard:
  `numeric-model-soundness`.
- **P3 — whole-corpus `--triage-bound` sweep: no genuine false proofs.** The 7
  `CBMC=SUCCESSFUL / expected=FAILED` candidates are all explained as opt-in-
  check coverage (`import-os2`, `input1/5`, `infer-func-no-return` -- needs
  --python-raising-ops-check / --python-check-annotations), model differences
  (`neural-net` fixedbv-vs-IEEE; CBMC matches CPython), `nondet_string`
  semantics (`string-nondet-...`; vacuous assume), or unsupported `encode`
  (`github_2993_2`). The other 26 DIFFs are CBMC=FAILED (sound spurious-fails);
  4 are BOUND (bug deeper than --unwind). The harness gained `--triage-bound`
  to auto-classify these.
- **P4 — type / isinstance / dispatch / Any-erasure: clean.** No false proofs
  (isinstance incl. tuple/inheritance, type narrowing, virtual/MRO dispatch,
  hasattr, Any-erasure attribute access); precise on positives. Guard:
  `type-dispatch-soundness`.


**Default int-overflow model-bound guard — ADDED (2026-06-25).** The default
64-bit int model silently wrapped on >64-bit results (a false proof, e.g.
`10**19 < 0`, `1<<70 == 0`). Mirroring the container-capacity guards, an integer
operation whose overflow is DEFINITE (statically provable: constant/folded
operands, constant non-negative shift) now reports a `python-model-bound`
(assert false + assume false -> cut) instead of wrapping
(`emit_int_overflow_guard` + binary_overflow_exprt on Add/Sub/Mult/LShift in
convert_bin_op and convert_aug_assign; exact mp_integer for constant `**`).
A POSSIBLY-overflowing SYMBOLIC op (`def f(a,b): return a*b`, `x+1` on nondet
x) is deliberately NOT guarded -- a blanket assert was far too noisy (it broke
11 tests incl. ones documenting the no-check default, and fired on bounded
comprehension arithmetic). Those remain the documented 64-bit bound;
--python-unbounded-ints is the sound mode (now also sound for shifts). Guard:
`int-overflow-literal-reported`.


**Int shift unbounded-mode false proof — FIXED (2026-06-25).** Under
`--python-unbounded-ints` (the SOUND int mode), `<<` and `>>` still truncated
the operand to signedbv64 before shifting, so `1 << 70` false-proved `== 0` and
`(2**70) >> 5` false-proved `== 0` even in the sound mode. Fix
(python_converter_ops.cpp): in the integer domain `x << n == x * 2**n` and
`x >> n == floor(x / 2**n)` (exact for a constant shift / non-negative operand;
sound nondet for symbolic shift or negative-operand `>>`). Guards:
`unbounded-shift-soundness`, `unbounded-shift-precise`. NOTE: the DEFAULT 64-bit
int model still wraps on >64-bit values (a documented bound, like `--unwind`);
`--python-unbounded-ints` is the sound mode and is now genuinely sound for
shifts too.

**String model + control-flow — full audit clean (2026-06-25).** P1 probed the
string model (constant + symbolic; refined default + native) across
find/index/count/replace/split/slice/startswith/in/==/ordering/case/join/format/
len/concat/mult/f-string — all sound (native times out on symbolic = perf, not a
false proof). P2 probed exceptions/`with`/generators (raise-flow, except-type
match, finally-return, `__exit__` suppression, re-raise, generator values/len)
-- all sound. Guards: `string-soundness-symbolic`, `control-flow-soundness`.


**Dict string-keyed value direct-mutation false proof — FIXED (2026-06-25, `b8ad0b63a6`).**
`d={"k":[1]}; d["k"].append(2); assert len(d["k"])==1` verified SUCCESSFUL
(real len 2): a string-keyed dict value is returned by copy, so the in-place
mutation is lost and the stale value is read. The extraction guard covered only
Name receivers; the direct subscript-receiver case was unguarded. Fix: havoc the
dict on a non-int-keyed in-place value mutation (int-keyed lvalue slot stays
precise). Found by the P2 adversarial dict probe. Guards:
`dict-value-mutation-soundness`, `dict-value-intkey-precise`.

**Set model — full audit clean (2026-06-25).** P1 probed every set
operation/projection for non-int false proofs; all sound after the earlier
membership-nondet + add-havoc fixes (`da7f7fbb85`, `00d563d7c6`). Guard:
`set-ops-non-int-soundness`.


**Set non-int false proof — FIXED (2026-06-25, `da7f7fbb85`).** A set is a 64-bit
int BITMAP (element -> bit position); a non-int element (tuple/str) cast to a bit
could COLLIDE with another element's bit, FALSE-PROVING membership
(`s=set(); s.add((1,2)); assert (3,4) in s` was SUCCESSFUL). Found by an
adversarial soundness probe while auditing the "set non-int model" residual.
Fix: non-int set membership -> nondet; set.add/discard of a non-int -> no-op on
the bitmap (int sets stay precise; non-int set literals already use a precise
list-backed representation). Guard: `set-non-int-soundness`.

**Kwarg cross-module binding — FIXED (2026-06-25, `6f9417b754`).** Not a false
proof (sound spurious-fail) but a correctness gap: keyword arguments to imported
functions (`mod.foo(a=5)`) were dropped (callee saw nondet/default). Now bound
like local calls; also closes the kwarg annotation check (github_3093_2). Guard:
`kwarg-cross-module`.


**Default-config false-proof triage + element-store audit (2026-06-25).** Two
proactive soundness passes (P1, P2). **Result: the default config is clean — no
genuine false proofs found in either pass.**

- **P1 — the 14 default-config "CBMC=SUCCESSFUL where expected=FAILED" DIFFs are
  NOT frontend false proofs.** Re-triaged each with adequate unwinding +
  `--unwinding-assertions` (the sweep's `--unwind 10 --no-unwinding-assertions`
  silently under-approximates). Categories: (a) **bound artifacts** — fail
  correctly at higher unwind (`github_2892`, `github_3836`, `global2`,
  `ethereum_bug`); sound w.r.t. the bound. (b) **float-model difference** —
  `neural-net`: CBMC's IEEE result `f==2.745` matches CPython (verified with
  `python3`); ESBMC's `--fixedbv` is the imprecise one, so CBMC is *correct*.
  (c) **`nondet_string` semantics** — `string-nondet-...-null`:
  `nondet_string(N)` is intentionally length-*exactly*-N, so an
  `assume(s==<shorter literal>)` is unsatisfiable → a (sound) vacuous proof;
  ESBMC's `nondet_string` differs. (d) **opt-in-check coverage gaps** — the
  property is only emitted under an opt-in flag, so the default has nothing to
  violate: `--python-check-annotations` incompleteness (float→int arg, cross-
  module, return-value type: `github_3020_5`, `github_3093_1/2`,
  `infer-func-no-return`) and `--python-raising-ops-check` (`input1/5`,
  `import-os2`), plus unsupported `encode` (`github_2993_2`). The annotation-
  check gaps are one coherent *precision* improvement area (not soundness).
- **P2 — element-store coercion audit (generalising the `extend` bug): no new
  false proofs.** Audited every container-mutation site that stores a value into
  a typed slot. `append` / `insert` / subscript-assign / dict-value-store /
  `*args` packing all coerce correctly; `extend` was the one genuine gap and is
  fixed (`8a618fbdac`). Remaining cross-type imprecisions are all **sound**
  (corruption → nondet, the wrong value is never provable — verified the
  false direction FAILS): `set.add` of a non-int (the set is a 64-bit int
  *bitmap* model — a fundamental limitation, not a coercion gap), slice-assign
  `a[i:j]=…` and `list()`-from-tuple (both broken even *homogeneously* — a
  separate list-construction precision family, not element-coercion). These are
  precision residuals, catalogued for future work. Lock-in test:
  `element-store-coercion`.


**Soundness re-audit (2026-06-24).** Deliberate audit pass (the call-duplication
hole was pre-existing; chasing features surfaced it, so a proactive sweep was
warranted).
- **Operand-duplication class — CONFIRMED CLOSED.** A side-effecting
  `python_value`-returning call duplicated in a lowering false-proved only in
  `==`/ordered (`73fce93762`) and membership `in` (`d210f808b0`), both fixed.
  Audited the rest (arithmetic, `str()`, subscript, boolean-op, augmented
  assignment, f-string, ternary, chained comparison): each returns a sound
  NONDET (precision loss, not a false proof — verified the wrong value is NOT
  provable). A mutation-count detector initially mis-flagged these; the
  value-based check confirms soundness.
- **Stub concrete-defaults — 2 value-dependent false proofs FIXED
  (`11926ba06f`).** `collections.deque.count/index/__len__` (fixed 0) and
  `time.process_time`/`_ns` variants (fixed 0/0.0 while `time()`/`monotonic()`
  are intercepted-nondet) -> sound nondet. Guards `stub-deque-count-fail`,
  `stub-process-time-fail`. Audited sound: `Counter[missing]==0` /
  `defaultdict(int)[missing]==0` (correct semantics), `contextlib.__exit__->
  False` (correct "don't suppress"), `math.isnan/gcd/...` (frontend-intercepted
  dead defaults). Residual: `threading.wait_for->True` (optimistic, but
  threading is an inherent sequential-BMC approximation); third-party stubs
  (numpy/pandas/...) out of scope.


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

### Library-stub soundness audit (2026-06-23) — concrete-default false-proof class

A systematic audit of `src/python/library/` for a single bug class: **a stub
method returns a fixed concrete value (`""` / `0` / `[]` / `{}` / `None` /
`(None,0)`) for a result that is actually VALUE-DEPENDENT**, which false-proves
assertions on that default (e.g. `json.loads(s) == {}`, `os.getcwd() == ""`,
`functools.reduce(...) is None`, `d.weekday() == 0`, `h.hexdigest() == ""`,
`struct.unpack("i",b)[0] == 0` all wrongly verified; each cross-checked against
CPython). This is the same class as the earlier **re-stub cluster**
(`re.escape`/`expand`/`groups`/`subn`/`groupdict`).

**Fixed (→ sound nondet of the right type; commits `632dfa81ff`, `ff1dfa7bd1`):**
`re` (earlier); `json` (loads/load/dumps/encode/decode/raw_decode/iterencode);
`functools.reduce`; `os` (getcwd/getenv/getpid·ppid·uid·euid·gid·egid/listdir);
`hashlib` hexdigest; `struct.unpack` (tuple VALUES; shape preserved);
`datetime` (toordinal/weekday/isoweekday/isocalendar/isoformat/strftime/tzname);
`tomllib`/`tomli` (loads/load); `configparser` (options/read/items/get-family-
no-fallback/has_option/remove_*); `csv` (has_header); `string` (Template
is_valid/get_identifiers, Formatter.parse); `argparse` (format_help/usage);
`pathlib` (exists/is_*/read_text/owner/group/glob/rglob/iterdir/suffixes/
write_bytes); `io` (readline/readlines/tell/truncate); `dataclasses`
(is_dataclass); `subprocess` (getoutput); `socket` (send/sendto/getsockopt/
fileno/getservbyname/gethostname); `logging` (Formatter.format); `traceback`
(format_*/extract_*). Validated: every batch left the ESBMC sweep byte-identical
to baseline (PASS 2706, 0 regressions); 657/657 local tests; ~18 guard tests
`stub-*-fail` + `re-*-fail`.

**Key learning — some stdlib calls are FRONTEND-INTERCEPTED** (computed
precisely), so their stub concrete-default is *never used* and is NOT a false
proof. Verified-and-left-alone: `math.factorial`/`comb`/`perm`/`gcd`/`lcm`/
`isqrt`, `time.time`/`monotonic`/`perf_counter`, `math.isnan`(concrete). **Every
suspicious default must be tested, not assumed** (cbmc-SUCCESSFUL + CPython-False
== confirmed; otherwise intercepted/legitimate).

**Left alone — LEGITIMATE fixed-value semantics (NOT bugs):** `contextlib`
`__exit__ → False` ("don't suppress"), `defaultdict.__missing__` factory-zeros,
`Counter` missing-key `0`, identity decorators (`lru_cache`/`wraps`/…),
`__init__ → None`, `bisect.insort → None` (and `bisect_left/right` return a real
computed index), `io` `readable`/`writable`/`seekable`/`isatty`. The audit
requires per-case judgment, not a blanket sweep.

**Documented residuals (sound today / out of stub scope):**
- **bytes-returning reads** (`io.read`/`readall`, `socket.recv`) — **FIXED**
  (`7d1e8f4a3c`): added a `nondet_bytes()` builtin (bytes is modelled as a list
  of u8, so it builds a bounded nondet u8 list), wired into io.read/readall +
  socket.recv. `read() == b""` now FAILs soundly.
- **`pathlib` path-DERIVED methods** (`name`/`suffix`/`stem`/`root`/`drive`/
  `anchor`/`as_posix`/`as_uri`/`__str__`/`__fspath__`/`is_absolute`/
  `is_relative_to`/`match`) still return `""`/`False`: these are *computable
  from the path string*, so the right fix is a PRECISE pathlib (compute), not
  nondet — making them nondet would regress `str(path)`/`name` usage. Precision
  follow-up, not a soundness-nondet target.
- **`inspect`** (~26 introspection methods) — same class, rarely asserted in
  verification; lower-priority, the guideline covers it.
- **`math.isnan`/`isinf` — NOT a bug (false alarm corrected 2026-06-23).** The
  earlier note claiming a symbolic-float `isnan` soundness bug was a *test
  artifact*: it used an UNCALLED function (`def f(x: float): assert not
  math.isnan(x)`) whose body is unreachable → vacuously SUCCESSFUL (no property
  checked). When the function is CALLED with `nondet_float()` (or at module
  level), `isnan` is fully SOUND — both `assert math.isnan(x)` and `assert not
  math.isnan(x)` correctly FAIL, and `x == x` FAILs (nan IS included in the
  nondet float domain; `isnan` dispatches to `isnan_exprt`). No fix needed.
  (Lesson: always verify a "false proof" is not a vacuous uncalled-function
  result before declaring a bug.)
- **Third-party integration stubs** (`numpy`/`pandas`/`flask`/`fastapi`/
  `sqlalchemy`/`requests`/`click`/`rich`/`yaml`/`attr`/`attrs`/`pytest`/
  `icontract`/`asyncio`) — vast APIs, not in the verification corpus; out of
  scope (audit on-use, applying the guideline).

**Stdlib audit status: COMPLETE** for the value-dependent concrete-default class
(the realistic verification surface); the residuals above are either a needed
builtin (`nondet_bytes`), a precision follow-up (pathlib paths), a separate
subsystem (frontend `math.isnan`), or out-of-scope third-party.

**Whole-group root + PREVENTION (stub-authoring guideline).** The root is a
recurring stub-authoring anti-pattern, not a single code site, so the
architectural fix is a guideline (there is no automatic detector — the frontend
cannot know a stub method is value-dependent):

> **A library-stub method that models a VALUE-DEPENDENT result MUST return a
> sound nondet value of the correct type** (`nondet_str()` / `nondet_int()` /
> `nondet_bool()` / `nondet_float()` / `nondet_dict(8)` / `nondet_list(8,
> sample)`), **never a fixed concrete placeholder** (`""`/`0`/`[]`/`{}`/`None`).
> A fixed concrete return is only correct when it is the genuine, input-
> independent semantics (a predicate that is truly constant, `__exit__ → False`,
> `__init__ → None`, a factory-zero). When in doubt, nondet is sound; a
> placeholder is a latent false proof. Container/`Match`-shaped returns should
> carry a return annotation (`-> dict`/`-> list`) or use the typed `nondet_*`
> so the return-type inference types the call site correctly.


### Soundness re-audit (2026-06-19, current binary, sweep PASS 2704/2832) — CONFIRMS no false proofs + sharpens the methodology

Re-ran every *expected-failure / got-SUCCESSFUL* row against the current
binary (after the L1 / async / complex / IEEE work). The 2026-06-09
conclusion holds: **no genuine algorithmic false proof** (we never prove a
buggy computation correct). Two methodology points make this precise:

- **The sweep config is unsound-by-construction for deep loops, so
  "SUCCESSFUL" is NOT a false-proof signal.** The harness runs `--unwind 10
  --no-unwinding-assertions`; when a loop needs more than 10 iterations the
  cut adds `assume(!guard)`, which *prunes* the continuing path, so
  post-loop assertions are vacuously SUCCESSFUL. Confirmed on
  `github_2892_fail` (a 28-char inner loop): SUCCESSFUL at `--unwind 10
  --no-unwinding-assertions`, but **FAILED** with unwinding assertions ON
  (default) and at `--unwind 35`. `github_3836_fail` / `global2_fail`
  likewise FAIL correctly at `--unwind 20`. A sound false-proof audit must
  use unwinding assertions ON.
- **Several "expected FAILED" tests are ESBMC-divergent and we are the
  PLR-correct side.** `neural-net_fail` asserts `f >= 2.745`; CPython
  computes `f == 2.745` (True), so the program **passes** — our SUCCESSFUL
  is correct and the test's FAILED expectation is an ESBMC float-model
  artifact. `--strict-types`-only (`github_3020_5`, `github_3093_1/2`),
  `--function` (`ethereum_bug-fail`, a `uint64` overflow — Python ints are
  unbounded), and `s.encode` "unsupported" (`github_2993_2_fail`, which
  CPython runs cleanly) are all ESBMC-mode/limitation expectations where
  SUCCESSFUL is the CPython-faithful answer.

**The one genuine soundness-direction theme (whole-group root):
incomplete `NameError` detection for definitely-undefined names.** CPython
raises `NameError` and we silently proceed: `assign-fail` (reads `q`,
never defined), `import-as-fail` (`mp`, a bad import alias), `return9-fail`
(annotation `-> UnknownType`, evaluated at def time). These share one root
— a `Name` (or annotation name) that resolves to **no** local / global /
builtin / imported binding should raise `NameError`, not fall through to
nondet. *Fix-shape (careful, whole-group):* at name resolution, when a
name is provably unbound on the executed path (not assigned anywhere in
scope, not a builtin, not imported, no `global`/`nonlocal`, no
star-import / `exec` in the module), emit an uncaught `NameError`. **Risk:
over-reporting** — dynamic definition, conditional assignment, and
star-imports make "definitely unbound" subtle, so this must be
conservative (only fire when no binding exists on *any* path) and
sweep-validated for new false positives.

**Attempt 2026-06-19 — REVERTED (over-reports); prerequisite identified.**
A first cut emitted the `NameError` directly in `get_var`'s "Unknown
variable" fallback, guarded by `!saw_import_star` + a full Python-builtins
exclusion set. It correctly flipped `assign-fail` and `import-as-fail` to
FAILED, but **regressed 4 tests** (`limit-tuple-return`,
`crash-nested-tuple-unpack`, `tuple-unpack-snapshot`, and
`snippet-undefined-var`). Root cause: **the assumption "reaching the
fallback ⇒ unbound" is false.** Names bound by tuple/list unpacking
(`x, y = f()`), `for`-targets, `with ... as`, `except ... as`, and
comprehension targets are tracked via side-tables (and their values flow),
but **no findable symbol** is created, so a later read reaches the fallback
and was wrongly flagged.

**LANDED 2026-06-19 (the safe foundation).** The fix is now gated on a
positive, authoritative oracle: the **AST server** (`python_ast_server.py`
daemon *and* the embedded fallback in `python_language.cpp`) computes
`_all_bound_names` — an over-inclusive set of every name bound anywhere in
the module across **all** binding forms (`Store`-context Names cover
assignment / tuple-or-list unpack / `for` / `with`-as / comprehension /
walrus uniformly; plus `def`/`class`/`arg`/`import`/`global`/`nonlocal`/
`except` names). `get_var` emits `NameError` **only** when a referenced
name is *absent* from this set — never on "reached the fallback".
Over-inclusion is the safe direction (can only miss a NameError, never
invent one). Further guards: restricted to main-module code (imported
modules have their own scopes), skipped under `from X import *`, and a full
builtin exclusion (functions + constants + `complex`, which `type_tags`
omits). *Validated:* `assign-fail` / `import-as-fail` now soundly FAIL,
`snippet-undefined-var` flipped to expect the NameError, new
`nameerror-bound-forms-ok` locks in that every binding form is not
misflagged; full `regression/python` + corpus sweep at **0 regressions**.
Niche cousins — **all LANDED 2026-06-19** (soundness-direction, 0 sweep
regressions). `return9-fail` (undefined name in an annotation) is now
handled by extending the `all_bound_names` oracle to **all** annotation
sites (return / parameter / variable) via `undefined_annotation_name()`:
a bare-name annotation bound nowhere raises `NameError` at def/statement
time. Guards: disabled under `from __future__ import annotations`
(PEP 563), bare-name only (Subscript / Attribute / string forward-refs
skipped), main-module only; module-level defs handled in
`convert_module_body` (they bypass `convert_statement`), nested defs +
`AnnAssign` in `convert_statement`.

**Cousins LANDED 2026-06-19** (soundness-direction, 0 sweep regressions):
* **`enumerate()` arity** — `enumerate()` with no iterable, or >2 positional
  args, now raises an uncaught `TypeError` (`enumerate3/4_fail`).
* **Compile-only SyntaxErrors** — a name used prior to its `global`/
  `nonlocal` declaration, a repeated keyword argument, and a duplicate
  parameter are genuine errors that `ast.parse` accepts but the compiler
  rejects; the AST server (daemon + embedded fallback) now runs `compile()`
  and surfaces ONLY this allow-list as a parsing error
  (`global-decl-fail`, `function-keyword-repeated-fail`), while
  deliberately tolerating top-level `await` and `break`/`return` in
  `except*`. Regressions: `enumerate-missing-iterable`,
  `syntaxerror-name-before-global`, `syntaxerror-repeated-keyword`.
* **`range()` arg-count + zero-step** — 0 or >3 positional args →
  `TypeError`; a zero step (literal or symbolic) → `ValueError`. A shared
  `emit_range_arg_checks()` is used at both range sites (the
  `for ... in range(...)` lowering and the `range()` builtin handler).
  Flips `range2/20/21/22/23-fail`. Regressions: `range-step-zero`,
  `range-bad-arg-count`.

> **Whole-group observation (builtin argument/value validation).** Four
> landed checks now share one shape — uncaught `TypeError`/`ValueError`
> when a call violates a callable's contract: unified call-signature
> validation (user functions), `complex()`, `enumerate()`, and `range()`.
> Each builtin's rules are bespoke (range step≠0, enumerate iterable
> required, …), so a single generic table is not obviously worthwhile yet;
> but if more builtins need it, a declarative per-builtin arg-spec
> (count-range + value constraints) feeding one emitter would consolidate
> them. Recorded so the pattern is recognised, not re-derived.

**Accepted-by-design (not bugs):** `float(input())` / `int(input())`
`ValueError` are covered by the opt-in `--python-raising-ops-check`
(default favours precision); annotation mismatches (`process(3.14)` into an
`int` param) are covered by opt-in `--python-check-annotations`. The
message-format FAIL rows (`github_3010*`/`3015*`, `casting*-fail`) are the
cosmetic output-format item — we detect the condition, we just don't
reproduce ESBMC's exact string.

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
opaque `__class_ptr` cast — see [#native-byte-ops](python-frontend-strings-plan.md#native-byte-ops)).

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

**Generator-object state (`.send()` / priming) — RESOLVED (2026-06-30, commit
`70401b6d90`).** The eager list model has no generator-OBJECT identity, but the
priming-state false proof is closed without one: the cursor already encodes
progress (`cursor == 0` ⟺ not yet started), so `gen.send(non-None)` on a
just-started generator now raises `TypeError`. See the **Phase 1 OUTCOME** below.
A genuine generator-object identity model (aliasing `it2 = it`, container slots,
`for`-after-partial-`next`) remains future work and **IS a known false-proof
cluster** (consumption-state; pinned 2026-06-30 — see the dedicated paragraph
after Phase 2 below). Passing a generator to a function is sound.

**Spike (2026-06-30, confirmed).** `it = g()` lowers to a call returning the eager
`__gen_result_g` list, plus a per-call-site cursor `__cursor_it` that `next()`
advances. So the "generator object" today *is* the list — there is **no
generator-object struct and no priming state**, and **`.send()` has no method
handler** (it falls through to a generic nondet method dispatch → no-op → the
false proof). Confirmed by `--show-goto-functions`: `CALL it := g(); __cursor_it
:= 0; …`. Both `it.send(5)` (must raise) and the valid `next(it); it.send(5)`
verify SUCCESSFUL today.

**PEP 342 / PLR §6.2.9 semantics.** A generator object starts *suspended before
its first line*. The first interaction must be `next(it)` or `it.send(None)`;
`it.send(non-None)` on a just-started generator raises `TypeError`. Thereafter
`it.send(v)` resumes execution and `v` becomes the value of the pending `yield`
expression (`x = yield 1` binds `x` to the sent value); `.throw()` raises at the
suspension point; `.close()` injects `GeneratorExit`.

**Phased plan.**
- **Phase 1 — priming-state TypeError (closes `gen_send_before_start`) — DONE
  (2026-06-30, commit `70401b6d90`).** *Shipped differently from this sketch — no
  `__started` flag.* The eager cursor already encodes priming: it inits to 0 at
  `it = g()` and `next()` does `cursor++` before returning `data[cursor-1]`, so
  `cursor == 0` *is* the not-yet-started state (single source of truth — a
  parallel flag would only risk desync). A new `.send()` method handler (none
  existed; `.send()` fell through to a no-op) resolves the receiver's cursor and
  emits `emit_conditional_exception(cursor == 0 ∧ arg≠None, "TypeError")`, then
  resumes like `next()`. *No-FP gating:* a provably-None send is never flagged; a
  concrete arg is never None; a `python_value` arg is guarded on its NONE tag; an
  opaque/aliased generator with no resolvable cursor is not flagged.
  *Whole-group fix discovered en route:* the eager model only appended `yield`
  *statements*, dropping `yield` *expressions* (`x = yield 1`), so such
  generators under-counted their yields — surfacing as a spurious StopIteration
  once `.send()` advanced the cursor. Factored `build_gen_result_append` and
  called it from BOTH `convert_expr_stmt` and `convert_expression(Yield)` so every
  yield is counted exactly once; this also fixed pre-existing false alarms (a
  two-`next()` yield-expression generator, +1 sweep case). *Validated:* suite
  green; sweep 2719 PASS / 0-reg (+1 new pass); oracle **1 → 0** false proofs.
  CORE: `gen-send-before-start-typeerror`, `gen-send-prime-nofp`.
- **Phase 2 — faithful `.send()` value-passing / `.throw()` / `.close()` (NO
  PLAN YET).** Making the *sent value* flow into the `yield` expression requires
  real suspension/resumption — the same state-machine encoding noted above. Only
  worth it if a benchmark needs faithful inter-yield value passing; Phase 1
  closes the soundness hole without it.

**Generator consumption-state / identity — KNOWN false-proof cluster (UNSOUND,
pinned 2026-06-30 via a proactive soundness sweep).** The cursor tracks
consumption only for a *direct* `next(name)` / `name.send()` on the original
call-site Name. Any other access path reads a fresh (cursor-0 / counter-from-0)
view and re-yields already-consumed elements — a false proof. Status:
- **CLOSED (2026-07-01):** `for x in g` after a partial `next(g)` now resumes
  from `g`'s cursor and exhausts it (`gen-foriter-after-next-typeerror`, CORE).
  The most common idiom; measured 0 sweep regressions / oracle 0-NEW.
- **CLOSED (2026-07-01):** an alias `it2 = it` now shares the consumption cursor
  (`gen-alias-consume-typeerror`, CORE) — the alias-assign path propagates
  `generator_cursors[it2] = generator_cursors[it]`, and since the alias is a
  pointer whose reads auto-dereference, `next(it2)`/`for` resolve the shared
  cursor on the deref'd struct.
- **OPEN** (pinned KNOWNBUG): a container slot (`box=[g()]; next(box[0])`,
  `gen-in-container-consume-knownbug`) and `list(g)`/`sum(g)` after a partial
  `next()` (aggregating builtins iterate via their own path).
Passing a generator to a function is sound (the param view is over-approximated
to nondet, not re-yielded). **Fix for the rest: a generator-OBJECT model** whose
consumption state (the cursor) is tied to the object and shared across all access
paths rather than keyed on the call-site Name — the same "identity, not
value/Name" move as the instance-reference-semantics cluster.
*Spike outcome (2026-07-01):* the `for`-resume and alias steps landed. The alias
fix's subtlety was finding the RIGHT alias-assign path: `it2 = it` is handled by
the main assign handler (not the `get_var_assign` path), and the cursor
propagation had to go there; once `generator_cursors[it2]` is set, the EXISTING
cursor path works because reads of the pointer-alias auto-dereference to the list
struct. The remaining container/aggregating channels each have their own
consumption path (container-subscript read, `list`/`sum` builtins) that would
each need cursor-awareness — the clean whole-group answer is to make the cursor a
FIELD of the generator's list struct so it travels with the object regardless of
access path (a representation change; deferred).

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

## 3–4. Strings & regex — moved

All `str`/`bytes` and `re` (regex) work now lives in the dedicated
[Python frontend strings & regex plan](python-frontend-strings-plan.md)
(native SMT-String backend, refined-string ceilings, regex). The
architecture gaps table links there directly.

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

## 7. `--python-check-annotations`: status, the `--python-strict` preset & the default-on prerequisite  {#check-annotations}

**Status update (2026-06-26): the two CBMC-core CRASH blockers are RESOLVED.**
Re-tested `--python-check-annotations` across the full `regression/python` suite
(717 tests) plus `websocket_url_validator` (the original solver-ERROR witness):
**0 flag-induced crashes / invariant violations** (the only rc≠0-without-output
cases were two `syntaxerror-*` tests, which fail identically without the flag).
The `boolbv_map` width-mismatch and per-property solver ERROR no longer
reproduce -- almost certainly closed by the leaf-boxing / per-instance allocation
/ native-string work landed across this arc. So the flag **no longer needs to be
off for stability**. Default-on is now gated only by **precision**: the checker
flags annotation mismatches that are not runtime errors when the value is never
misused (Python checks no annotations at the call; the `greet(42): pass` case),
so turning it on by default would add spurious failures on legal code. The precision cost is now
**quantified (2026-06-26)**: forcing `--python-check-annotations` on flips
**43 / 2718 ESBMC-corpus tests PASS→DIFF (~1.6%)** and **5 / 577 success-expecting
`regression/python` tests (~0.9%)** to spurious failure. An initial sampled
classification suggested **most of these were FIXABLE checker false positives**
— but this estimate was **superseded by the 2026-06-26 re-measurement after the
fixes landed** (see the updates further down): once the fixable checker-bug
classes were removed, the remaining residual is in fact **mostly INHERENT** (real
annotation mismatches that are not runtime errors). The initial fixable-looking
categories were:
  - flagging an **Any / unannotated-function-return RHS** (`a: int = unann()`,
    `d: Dict[str, Any] = {...}`) — the checker should not flag a `python_value`
    source;
  - flagging an **inferred parameter** type (lambda params, `*args`/`**kwargs`,
    argparse-style flexible signatures) — the same annotation-provenance gap the
    call-boundary obligation already solved (`annotation_types_incompatible` is
    NOT provenance-gated);
  - not recognising a **builtin return type** (`r: range = range(4)`,
    `dict[Any,Any] = {comprehension}`).
The genuinely **inherent** cost (a real mismatch that is not a runtime error
because the value is never misused as the annotated type, e.g. `x: int = f()`
where `f()->str` and `x` is used as a str) is the smaller remainder.

**Path to default-on:** fix the checker false positives above (reuse
`explicitly_annotated_params`-style provenance to skip inferred params; skip a
`python_value`/Any RHS; recognise builtin/`range` return types), then re-measure
— the residual inherent cost should fall well below 1%, at which point a
default-on (or a `--python-strict` preset) annotation soundness mode becomes
viable.

**Update (2026-06-26, FP-reduction pass).** Fixed the call-argument checker-bug
class: the call-arg annotation-mismatch checks are now provenance-gated (reuse
`explicitly_annotated_params`), so an inferred/default param (lambda,
`*args`/`**kwargs`, argparse) is no longer flagged; this also required populating
`explicitly_annotated_params` for METHOD parameters (separate param-processing
path). Result: curated-suite FPs 5->3, corpus FPs 43->40, no default-mode
regression (`calc.multiply(5,"ten")` and the cross-module mismatch still
detected). The re-measurement reveals the REMAINING cost is mostly INHERENT --
real mismatches the flag is designed to catch but that are not runtime errors
because the value is never misused (`a: int = <float>`, `x: int = f()` where
`f()->str` and x is used as a str -- the github_3775 / function-keyword /
recursion / while / sequence clusters). The smaller fixable remainder is
`range`/builtin-return recognition (`r: range = range(4)`, github_3751*) and
Any-valued-container AnnAssign (`dict[K, Any]`), both rooted in
`convert_type_annotation` fallbacks whose naive fix has DEFAULT-mode side effects
(a `dict[K,Any]->dict` attempt regressed `dict-if-not-in-idiom`), so they need
careful separately-validated handling. The inherent floor is the real default-on
gate.

**Update (2026-06-26, the `convert_type_annotation` unknown-fallback fix).**
Found and fixed a genuine WHOLE-GROUP architectural defect: `convert_type_annotation`
overloaded `python_int_type()` as BOTH the legitimate `int` type AND the
"I-can't-model-this-annotation" fallback. That single collision caused two
otherwise-unrelated-looking symptoms:
  - **PLR soundness (default mode):** an unknown-typed value was modeled with
    *concrete int semantics* instead of being over-approximated to the top type.
    This is a latent unsoundness -- and it was real: flipping the fallback to
    `python_value` made the by-value sweep *gain* `ethereum_bug-fail` (the int
    fallback had been masking a genuine bug).
  - **`--python-check-annotations` FPs:** the checker read the unknown-fallback
    `int` as a precise `int` declaration and flagged the real value (a `range`,
    a bare `tuple`, an unknown forward-ref) as a mismatch.
The fix: the unknown-annotation fallback is now `python_value` (Any / top) -- the
sound over-approximation, which the checker already treats as
compatible-with-everything. Applied to the unknown-forward-ref string, the final
catch-all `else` (covers `range` etc.), and bare `tuple`. Left the intentional
`Constant None -> int` (None sentinel) and `enum-without-value -> int` untouched.
Validation: full default suite green; by-value sweep PASS 2719 (baseline 2718),
0 regressions + the `ethereum_bug-fail` gain; oracle 0 NEW false proofs.

Cumulative corpus FP under the flag this session: **43 -> 34** (call-arg
provenance gating -3, range/unknown -> Any -4, bare tuple -2). The remaining 34
are now dominated by the INHERENT narrowing class (real mismatches the flag is
designed to catch but that are not runtime errors: `github_3775*` `int=<str>`,
`function-keyword*` `int=<float>`, `recursion`/`while`/`sequence`, `inheritance2`
`sound: int = dog.bark()`).

**The one remaining shared-root case that is NOT fixed -- and why (a genuine
deeper entanglement, not a point fix).** The dict-with-non-"safe"-value fallback
(`dict[str, Optional[int]]`, `dict[str, Any]`, `dict[Any, Any]` -- `github_3658_6`,
`any-dict-subscript`, `crash-comprehension-type-reuse`) is the SAME int-collision
root, but every faithful fix (dict-with-`python_value`-values, or whole-thing
`python_value`) regresses `dict-if-not-in-idiom`. The failure was diagnosed and
is NOT the KeyError elision (that elision is AST-structural and type-independent):
it is `[python-model-bound] container capacity exceeded` on
`providers[key].append(item)` once the dict values become Any. So the Any-valued
dict fix is gated behind a *separate, deeper* Any-valued-CONTAINER
capacity-modeling representation issue, which must be addressed on its own (and
validated) before the dict-value-nonsafe fallback can be flipped. Tracked here as
the next step for this cluster, and pinned by the KNOWNBUG regression test
`regression/python/check-annotations-any-dict-knownbug` (desired: SUCCESSFUL;
currently the spurious FAILURE — promote to CORE when the representation issue is
fixed).

**Update (2026-06-26, `--python-strict` landed).** Decision taken: ship the
annotation-strictness family as the opt-in preset `--python-strict`
(= `--python-check-annotations` + `--python-missing-return-check` +
`--python-required-kwarg-checks` + `--python-check-typeddict-fields` +
`--python-check-any-arg-attrs` + `--python-check-iter-none`) rather than
flipping any of them on by default. Rationale: the residual precision cost is now
mostly INHERENT (real annotation mismatches that are not runtime errors), so
default-on would change what `VERIFICATION FAILED` means (conflating a runtime
fault with a static annotation mismatch CPython tolerates) — a category change,
not a tunable FP rate. The preset is additive (no default-semantics change) and
deliberately does NOT imply `--python-raising-ops-check` (a separate
runtime-exception-soundness axis). **The prerequisite for any future *default-on*
decision is use-site misuse gating**: only flag an annotation mismatch when the
value is actually used *as* the annotated type in a way that causes a real
runtime fault (`n: int = "x"; n + 1`) and stay silent otherwise — that converts
the inherent floor from false-positive to correctly-silent and re-aligns the
property with runtime semantics. That gating is the tracked next step for this
cluster.

**Update (2026-06-26) — use-site misuse gating is ALREADY the default behaviour
(reframes the default-on question).** Investigating how to build use-site gating
revealed it largely already exists: in DEFAULT mode (no `--python-check-annotations`)
the runtime obligation system (operator operand-type / subscript / call-arg tag
obligations) catches an annotation-mismatched value *exactly when it is misused
at a use site*, and stays silent when it is not — verified:
`n: int = "x"; n + 1` → **FAILED** (caught), `n: int = "x"` unused → **SUCCESSFUL**
(no FP), `s: str = 5; len(s)` → **FAILED**, `s: str = 5; x = s` → **SUCCESSFUL**.
This is precisely the "flag only on misuse" semantics, and it is keyed on the
value's ACTUAL runtime type (PLR-correct: annotations are not runtime coercions),
independent of the annotation. So **there is no large new feature to build, and
no soundness reason to make `--python-check-annotations` default-on** — runtime
type-misuse is already caught by default; the flag adds *static declaration*
strictness (mypy-style), which is correctly opt-in. Remaining default-mode
use-site gaps are the SAME documented clusters, not new work: (a) a concrete
mismatched scalar passed to an annotated param is *punned* by boundary coercion
(`s: int = "x"; f(s)` with `f(k: int)` — the coercion-boundary PUN family, same
root as list-element/attribute-field), and (b) some operand-type corners (now
including `str` bitwise, closed 2026-06-26). Conclusion: the default-on question
is **resolved** — keep `--python-check-annotations` opt-in (static strictness);
the runtime-soundness goal is met by the default obligation system, and is
extended incrementally as the coercion-boundary PUN cluster is addressed.

The decision The original blocker write-up is kept below for the
record.

**Original (now-resolved) blocker write-up.**
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

**Coverage-extension backlog (the whole-group view of `--python-check-annotations`).**
The checker fires at the call-argument and annotated-assignment boundaries, but
NOT yet at the **container-element** boundaries: `list[int].append("s")` (the ty
`007` laundering witness) and dict-value stores. Extending it there would let
the flag catch the *whole* coercion-boundary laundering cluster (the architectural
prerequisite for ever making it default-on). **Implementation note / blocker
(2026-06-26):** the natural AST hook (`convert_expr_stmt`'s append/insert mutator
block) cannot safely `convert_expression` the appended argument just to read its
type — a side-effecting arg (`xs.append(src())`) would be converted twice. A
correct implementation needs the single append element-coercion site (currently
dispersed across the method-dispatch path) so the already-converted element +
the list's declared element type can be compared once. Scoped, opt-in, sound (gated on the flag).
**Partially landed (2026-06-26):** `--python-check-annotations` now flags
`list[T].append(v)` / `insert(_, v)` when `v` is a **Constant or Name** whose
type is incompatible with a concrete element type `T` (idempotent to convert, so
no side-effect double-evaluation) -- guard `check-annotations-list-append`.
**Still deferred:** a **Call** argument (`xs.append(src())`, the exact ty-007
shape) is skipped to avoid double-evaluating a side-effecting expression; closing
it needs the single (currently dispersed) append element-coercion site so the
already-converted element can be compared once. Dict-value stores likewise
pending.

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

### dict symbolic-key / value-mutation cluster — characterized MULTI-root (2026-06-26) {#dict-cluster}

Investigated the "symbolic-key dict precision" cluster end-to-end; it is **not a
single architectural root** — four distinct sub-cases:
1. **symbolic-key build + constant-key read** (`for k,v in src.items(): d[k]=v` then
   `d["const"]`) — **already works** (the `github_3684` shape verifies), and a read
   of a definitely-unwritten key still raises `KeyError` (soundness intact).
2. **int/bool-keyed constant re-store** (`d={1:10}; d[1]=20; d[1]`) — **FIXED**
   (commit dropping the string-only const-fold for the dict on a non-string
   constant-key store, so the read uses the updated runtime array;
   `dict-int-key-restore`). Was a stale-read precision bug.
3. **dict-VALUE in-place mutation** (`d[k]=[]; d[k].append(x)`; also literal
   str-keyed `{"a":[10]}; d["a"].append`) — **OPEN**: the subscript-read value is
   a by-VALUE copy, so the append does not propagate (only the int-*literal*
   value happens to alias via the int-keyed lvalue value-slot). This is the
   documented **dict-value-by-reference** limitation (per-instance value
   identity) — see [dict-value-byref plan](python-frontend-dict-value-byref-plan.md).
4. **genuinely symbolic/nondet key** read — **OPEN**: over-approximates to a
   nondet value/length, so a downstream `.append` capacity guard or `KeyError`
   fires spuriously. Deep symbolic-dict-modelling work.
Sub-cases 3 and 4 are deep (not point fixes); 1 and 2 are resolved.

### Corpus precision-gap DIFF triage (2026-06-23) — `got=FAILED exp=SUCCESSFUL`

Triaged the sweep rows where we FAIL a program that should verify (the
user-visible "raise the PASS count" set). Finding: these are a **long tail of
individually-rooted moderate/hard cases, NOT a clean whole-group** — in
particular the `github_3560` family (initially hoped to be one easy root) is
**hard on both backends**. Per-case root cause:

- **`github_3560` / `_1` / `_3` / `_4` (symbolic `split(sep, maxsplit=1)`)** —
  `(s + ",end").split(",", 1)` with `s = input()`. **HARD, both backends:** the
  refined string solver cannot prove the substring relations of even the
  hand-expanded `find`+slice model on a symbolic concat (refined-string
  ceiling); native (`--cvc5`) either returns nondet for `split` or **times out**
  on the `find`+`substr`+concat+equality. A bounded `maxsplit=N` split IS
  exactly `N` `find`+slice ops in principle, but neither backend discharges it
  on a symbolic subject today. Research-grade (symbolic-string reasoning).
- **`function-default-function-var` (PLR §8.7) — FIXED (`32bf9f7acd`).** Root was
  NOT the default value-freeze (that worked) but **higher-order
  monomorphisation**: a callable-valued NAME default (`operator=cur`, `cur=mul`)
  was dispatched to `cur`'s *current* binding, so after `cur=sub` the defaulted
  call `do_op(2)` was specialised to the `sub` variant. Fix: `convert_module_body`'s
  def-time freeze loop now also snapshots the callable a Name default resolves to
  (def-time `function_aliases`), and `try_monomorphise_call` consults that
  snapshot before live resolution. +1 corpus PASS, 0 regressions; guard
  `func-default-callable-reassign`.
- **`forward-declaration5` — FIXED (`007bd7c53a`).** Was NOT scoping: a
  **forward-reference in return-type inference**. Sub-pass 1b registered
  unannotated functions with the int default and the real type was set only in
  source order during body conversion (1c), so `f` returning `g()` (with `g`
  defined later) was typed against g's int default → downstream false alarm
  (`f() == "global"` folded to constant-false). Fix: new sub-pass **1b.4**
  infers unannotated return types from the body (constants, calls to user
  functions / class constructors) and iterates to a **fixpoint** so forward
  tail-call chains (f→g→…) converge before bodies are converted; only refines a
  determinable type (conflicts→`python_value`, undetermined→unchanged), so it is
  sound. +1 corpus PASS, 0 regressions; guard `forward-ref-return-type`.
  Forward refs whose callee returns a CONTAINER (dict/list/tuple) are now
  ALSO handled (`f0c…` follow-up): sub-pass 1b.4 was unified to call the same
  `infer_return_type_from_body` as 1c (which gained bare-constant + tail-call
  cases), so containers/tuples/class-constructors/tail-calls all resolve
  across forward references with 1b and 1c in exact agreement. (That unify
  also tightened the resolver: a value-return + a None-return now always
  yields `python_value` / Optional regardless of scalar type — fixing a
  transient regression in the `github_3563*` Optional-dispatch tests.)
- **`builtin_all_genexp_inner_iter_shadow` — re-diagnosed: NOT scoping.**
  Shadowing works (the list-comp form `[x for x in xs for x in range(x)]`
  passes). The failure is **`all()`/`any()` folding over a 2-generator genexp
  when the two loop vars are the SAME (shadowing) AND the inner iterable is a
  data-dependent `range(x)`**: the 2-generator fold path
  (`call_builtins.cpp` ~3102) explicitly bails on `outer_var == inner_var` and
  only handles static list/Name inner iterables, so it falls through to the
  single-generator path which ignores the inner `for`. Narrow, intricate
  fold-unroll extension (not a group); deferred.
- **`github_3594`** — `"ß".upper() in ("SS","ẞ")`: Unicode case-mapping
  (ß→SS); `upper` is ASCII-only. HARD (Unicode case maps; see the strings-plan
  `casefold`/`title` residual).
- **`constants` (`uint64(...)`), `gb-2915` (local package `import l; from l.ks
  import foo`)** — test-specific (an unusual width-typed builtin; a local
  package-import path), not general gaps.
- **`complex_constructor_extended` / `complex_math_typeerror_edges`** — complex
  precision; `complex(<non-literal str>)` parse is the strings-plan residual.

**Net:** no single fix raises many; the largest cluster (`github_3560`) is the
refined-string-ceiling / native-perf-cliff, already the documented strategic
hard problem. Recommended order if pursued: `function-default-function-var`
(clean §8.7 fix) → the two scoping cases → defer the symbolic-split/Unicode/
complex ones (hard) and the test-specific ones.

**Update (2026-06-23, soundness + flag-artifact + string-method pass):**
- **Soundness bucket cleared.** The `got=SUCCESSFUL / exp=FAILED` DIFFs have no
  default-config false proof: `github_3836_fail` FAILS correctly under the
  sweep's `--unwind 10` (CSV verdict is a harness artifact); `global2_fail` is
  an unwind-bound artifact (`range(15)` needs unwind ≥ 15); the rest are
  flag-dependent (`--python-check-annotations`, `--python-raising-ops-check`,
  `--fixedbv`); `string-nondet-embedded-null` is sound under our documented
  `nondet_string(N)` == EXACTLY-length-N contract (`nondet_str()` covers
  [0,15]).
- **Flag-artifact finding (REVISED 2026-06-23).** Initial impression was that
  the DIFF count was heavily inflated by the sweep's uniform `--unwind 10` vs
  per-test flags. A proper re-audit (running each `got=FAILED/exp=SUCCESSFUL`
  DIFF with its OWN unwind + check flags, dropping only solver/`--ir`/
  `--smt-during-symex`/`--incremental-bmc`/`--nondet-str-length` which our cbmc
  rejects) shows **30 of 32 still FAIL** — they are GENUINE precision gaps, not
  config artifacts (only 2 inconclusive: `nondet_list4/5`). So the precision
  backlog is real, ~30 gaps, clustered: symbolic-split `github_3560*` (4, hard);
  string `string-casefold-accent`/`github_3594` (Unicode, hard) +
  `string-index-empty-inverted`/`string-replace-count-nondet-success`
  (tractable); nondet/list `nondet_list2`/`list_extend12,17`/`range36-nondet`/
  `set_from_param`/`github_3719_4,5-nondet`/`github_3783_5-nondet` (~8, varied);
  complex (2); import `heapq_import`/`import`/`github_3667`; misc
  (`builtin_all_genexp_inner_iter_shadow`, `constants` uint64, `gb-2915`,
  `global`, `int_subclass`, `loop-invariant2`, `method-instances`,
  `object-empty-not-found`, `github_3701_14`). (Unwind-bound artifacts DO occur
  in the OTHER bucket — `global2_fail` needs unwind ≥ 15 — but not here.)
- **`str.replace` empty pattern — FIXED, constants + symbolic length
  (`9fdc5906e9`, `79525a4702`).** Constants compute the exact result
  (`"a".replace("","x")=="xax"`); a symbolic source with a constant empty
  pattern binds the exact result length `len(src)*(1+len(new))+len(new)` on both
  back-ends (content nondet, sound). Flipped `string-replace-empty-nondet-success`
  (PASS 2709). Guard `string-replace-empty-pattern`. RESIDUAL:
  `string-replace-count-nondet-success` is a different case (non-empty pattern +
  CONTENT assertion on an `assume`-constant string → needs assume-folding);
  `string-index-empty-inverted` needs `str.index("",start,end)` inverted-bounds
  `ValueError`. The "empty-arg" cluster is themed, not single-root.

**Update (2026-06-23, nondet/list cluster triage + string edges):**
- **nondet/list cluster is mostly NOT tractable point-bugs.** Triage:
  - **`github_3719_4/5` + `nondet_list2` (float) — SOUND NaN divergence, NOT
    bugs.** `nondet_float()` includes NaN (correct PLR), so `v == y` / `elem ==
    elem` are legitimately false for NaN; with NaN excluded (`assume(y==y)`)
    they pass. Our FAILED is the SOUND verdict — "fixing" to match the tests
    would require excluding NaN (unsound). MUST NOT fix.
  - **`range36-nondet`** — `len(range(n)) == n` fails for n > the materialised
    cap (range is built as a bounded list); proper fix = lazy/symbolic range
    length, a large change. (Negative-n is handled correctly: `n<0 →
    len==0`.)
  - **`set_from_param`** — `set(s)` over a string PARAMETER called with 2
    different constants; needs per-call-site specialisation/inlining (the
    constant `len(set("aaa"))==1` works). Moderate-hard.
  - **`github_3783_5` (popitem)** — real bug but deep: dynamically-added dict
    string keys are stored as pointers to distinct temp char arrays, and
    `cprover_string_equal_func` doesn't relate `keys[i]` to the literal key
    (popitem returns the right VALUE but a wrong KEY; literal-dict popitem +
    `key in d.keys()` both work). Refined-string-representation issue.
  - **`list_extend17`** — `list.extend(<generator expr>)` unsupported (extend
    with a list works). Moderate.
- **`str.index`/`find` empty-substring bounds — FIXED (`63976ea006`).** Empty
  sub is found at `start` only when `start<=len && start<=end`; inverted/
  out-of-range bounds raise ValueError (index) / return -1 (find). Also: the
  index/rindex ValueError now sets the exception TYPE so `except ValueError`
  catches it (benefits ALL index ValueErrors). Flipped
  `string-index-empty-inverted` (PASS 2710). Guard `string-index-empty-bounds`.

**Update (2026-06-23, precise-pathlib BLOCKED + Tier-4 triage):**
- **precise-pathlib — BLOCKED by two frontend limitations** (stub-only attempt
  reverted; would otherwise regress pathlib). To compute `name`/`suffix`/`stem`/
  `__str__`/`is_absolute` from the path string a stub must (a) capture the
  `PurePath(*parts)` constructor args and (b) run string ops on the stored
  path.
  1. **`self, *parts` varargs binding — FIXED (`9a6de18316`).** Method/
     constructor calls now pack trailing positionals into the `*args` list
     (`build_class_init_call`), so `len(parts)`/`parts[0]`/`name, *rest` work
     (guard `init-varargs`). Whole-group fix: any `*args` method/constructor.
  2. **constant-string tracking through instance attributes — PARTIAL.** A
     PARAM-stored attribute folds (`self.r = s; c.r.upper()` works), but a
     literal-stored one (`self.r = "x"; c.r.upper()`) and a *parts/loop-built
     `_raw` do NOT (constant tracking doesn't survive the join loop).
  3. **NEW BLOCKER — accessor string-ops perf.** With (1) fixed, a single-arg
     precise pathlib works (verified: `PurePath("/usr/lib/foo.txt").name ==
     "foo.txt"` etc.). BUT the multi-arg case (`PurePath("a","b","c")`, in the
     `python-library-functools-pathlib-enum` test) builds a NON-folding `_raw`
     via the join loop, and the accessors then run SYMBOLIC `rfind`/slice on it
     → solver blowup / **timeout**. The stub can't cheaply do "fold if constant,
     else nondet" — that needs a frontend string-method guard
     (constant-fold-else-cheap-nondet). So precise-pathlib was reverted again;
     it stays at the sound (nondet/`""`) fallback. Remaining prerequisite is now
     a FRONTEND `extract_string_value`-fold-or-bail guard for str methods, not
     varargs.
- **Tier-4 individual-DIFF triage:**
  - **`object-empty-not-found` — FIXED (`b46f2bff8d`)**: `set.pop()` now returns
    a bitmap-constrained element (precise for singletons, sound for multi).
    +1 PASS (2711).
  - `complex_constructor_extended` — fails only at `complex("5+6j")` from a
    NON-literal string variable: the documented hard `complex(<symbolic str>)`
    parse residual (strings plan).
  - `github_3667` — nested-list shallow `copy()` aliasing (`nested[0].append`
    seen through `shallow[0]`): the documented HARD per-instance-identity
    problem.
  - `method-instances` (unbound method with POSITIONAL self —
    `MyClass.m(inst)`; keyword `self=inst` works), `int_subclass`
    (`class X(int)`), `heapq_import` (heapq ops): moderate individual gaps.
  - `loop-invariant2` (`__loop_invariant` + 5e6 loop), `import`/`global`
    (local-module import): flag/feature/test-specific.

**Update (2026-06-23, constant-string propagation — premise CORRECTED):**
Investigated "constant-string propagation through attributes and call params"
as a unifying whole-group fix. **The call-param half is mostly ALREADY SOLVED**
— multi-call propagation with DIFFERENT constants works for `return s`,
`s.lower()`, `len(s)`, `s[0]`, `"a" in s`, `s + "!"` (verified passing with two
distinct constant args). It FAILS only for **container-producing ops that
materialise per-character at CONVERSION time**: `set(s)`, `sorted(s)`,
`s[::-1]`. Those need `extract_string_value` to succeed (a conversion-time
constant), which single-call 1b.5 supplies (`string_constants[param]`) but
multi-call (runtime-propagated args) does not. So:
- The real call-param gap is narrow (~1 corpus test, `set_from_param`) and would
  need per-call VALUE-monomorphisation (clone f per distinct constant arg,
  seeding `string_constants` in the clone) OR runtime-materialisation of
  `set`/`sorted`/reverse over a bounded symbolic string. A large/perf-sensitive
  change for a 1-test payoff — NOT justified now.
- The **attribute half** remains blocked by per-instance struct-value tracking
  (the documented hard per-instance-identity problem); there is no
  instance-literal map analogous to `list_literals`/`dict_literals`.
Conclusion: this is NOT the large whole-group lever it appeared to be — the
common cases already work. Recorded to prevent a mis-targeted effort.

**Update (2026-06-23, #1 string-iteration ops + #2 individual gaps):**
- **`list()`/`reversed()`/`sorted()` over a string — FIXED (`28706f7918`)**:
  iterate code points into a list of single-character strings (constants;
  `sorted("cba")` failed even for a literal before). Correctness fix, no corpus
  flip.
- **pure `int` subclass — FIXED (`4d09b5a812`)**: `class U(int)` (no own attrs)
  modelled as int for both construction (`U(5)→5`) and annotation (`x: U →
  int`). +1 PASS (`int_subclass`, 2712). Limitation: an UNannotated `x = U(5)`
  still infers x as the struct.
- **unbound instance-method positional self — FIXED (`0b3fb25977`)**:
  `Class.method(inst, ...)` now binds the explicit first positional as self
  (gated on first param == `self`, so classmethods` `cls(...)` are untouched —
  caught + fixed an intermediate `classmethod-cls-construction` regression).
  +1 PASS (`method-instances`, 2713).
- **`heapq` — BLOCKED by list sort/del pass-by-reference.** The stub models
  ops via `heap.sort()` / `del heap[0]` / `heap.append(...)`.
  **FIXED (`6091534cd4`, `73fce93762`, `48245ddbd9`).** Closing heapq took four
  layers, all now resolved:
  1. `sort` by-ref: write the sorted elements IN PLACE (not a whole-struct
     reassign) and compare the by-ref container's python_value `__int_val`
     payload (a raw `>` on the structs didn't compare values).
  2. `del l[i]` by-ref: unwrap the by-reference container to the shared list
     lvalue before the in-place shift.
  3. module/stub call by-ref: annotate the heapq stub params as `list` so the
     call site promotes the argument to a by-reference container with
     write-back (same path user-function list params use).
  4. **SOUNDNESS — call-duplication (`73fce93762`):** a python_value operand of
     a comparison is referenced twice in the lowering (tag predicate + payload
     unwrap); a side-effecting call (e.g. `heappop`, which pops a by-ref list)
     was RE-EVALUATED and the two evaluations DIVERGED → a FALSE PROOF
     (`f(h) == <wrong>` provable). Pre-existing hole, surfaced here; fixed by
     materialising such an operand into a temp once. Guards `call-eval-once`,
     `call-eval-once-fail`. heapq_import → PASS (sweep 2714, 0 regressions).
     Residual: float/mixed-payload sort still compares only `__int_val`.

**Update (2026-06-23, call-duplication class CLOSED + complex-from-var):**
- The side-effecting-call double-eval false-proof class is now fully closed:
  `==`/ordered/arithmetic (`73fce93762`) and **membership `in`** (`d210f808b0`,
  materialise a container that embeds a side-effect). Audited sound: subscript,
  boolean-op, augmented assignment, f-string. Guards `call-eval-once-fail`,
  `membership-eval-once-fail`.
- **`complex(<non-literal str>)` — FIXED:** parse via `extract_string_value`
  so a variable holding a constant string is parsed, not just a literal.
  complex_constructor_extended -> PASS (2715).

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
    ([§3](python-frontend-strings-plan.md#strings)), not a fragile point fix.
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
      string-refinement root ([§3](python-frontend-strings-plan.md#strings)).
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

> **Unified extraction-aliasing root (analyzed 2026-06-22).** The `r = g[i];
> mutate r` pattern here is the *same* residual as `v = a[k]; v.append(...)`
> for dicts (d3). **Direct** nested mutation now works for both containers
> via lvalue slots (lists already; dicts per
> [dict-value-byref](python-frontend-dict-value-byref-plan.md)); the shared
> residual is **extraction-then-mutate**. A slot-pointer fix
> (*by-reference-at-extraction*) was investigated and found **unsound**:
> CPython's `r = g[i]` aliases the *object*, not the slot, so it diverges
> under subscript-assign / `insert` / `pop` / `sort` / rebind /
> cross-function reassignment — a **false proof** (see the
> dict-value-byref doc for the worked CPython examples). The only sound fix
> is per-object identity = the **byref-at-construction** substrate already
> found perf-untenable, so this residual is **blocked on the same
> representation barrier**, not a missing point fix. It stays a sound,
> guarded, corpus-invisible residual.

### List-precision cluster — implementable plan (empirically triaged 2026-06-19) {#list-precision}

Triage of the spurious-failure DIFFs (`list-sort7`, `list_extend12`,
`nondet_list2/4/5`, `list13`) against the current binary shows these are
**distinct roots, not one architectural fix** — recorded honestly so each
is actioned on its merits. Whole-group vs point is flagged per item.

1. **Mixed `int`/`float` `.sort()` mis-orders — FIXED (2026-06-19).**
   `[3, 1.5].sort() == [1.5, 3]` FAILED because a mixed list is stored as a
   `python_value` tagged-union struct array and the bubble-sort fallback
   compared whole structs with `>`, ordering by the **tag** field first
   (every int before every float). The element *equality* path already
   promotes across the union; the sort comparator did not. *Landed:* the
   constant-fold sort path gained a value-keyed numeric branch — classify a
   list whose every element is a constant numeric (raw scalar or a
   `python_value` INT/FLOAT/BOOL struct), read the active field via
   `try_eval_double`, and `stable_sort` by that double key while preserving
   each element's original int-/float-typed expr (CPython reorders the same
   objects; stable for equal values like `2.0`/`2`). One change covers the
   mixed-numeric constant `sort`/`sorted`. *Validated:* gained `list-sort7`,
   `list-sort-mixed-numeric` regression added, full `regression/python` +
   corpus sweep at **0 regressions**. *Residual:* the **symbolic** (non-
   constant) mixed-numeric bubble sort still compares whole structs (tag-
   ordered); no failing corpus test exercises it — left as a documented
   follow-up (a cross-tag numeric comparator in the bubble path, mirroring
   `python_converter_compare.cpp`). *PLR:* CPython orders int/float by
   numeric value; never by tag.

2. **`extend([literal] + param_list)` value precision — ROOT SHARPENED
   2026-06-19; narrow inline-fold, shared boxed/unboxed representation.**
   The real root is **not** in `extend`/concat: a list *parameter* is
   reconstructed as a `python_value`-**boxed** element array (`__byref_cont`,
   elements `{tag,val,…}`), whereas a homogeneous literal `[5]` keeps
   **unboxed** raw-typed elements. Reading one element (`r[0]`) and `len(r)`
   unbox fine, and the runtime list-`==` has a `can_bridge` path that
   unwraps — so the only failing shape is the **inline** fold
   `f([5]) == [5]` where `f` returns its parameter *directly*: that folds to
   a constant `false` (boxed-vs-unboxed struct compare) instead of taking
   the bridge. Materialising the result first already works:
   `y = f([5]); y == [5]` PASS, and `s = r; return s` PASS. So real-world /
   corpus impact is marginal (the corpus `list_extend*` DIFFs are L3
   recursion or generator+nested-list under SMT-only flags, not this).
   *Fix-shape (deferred, fold-sensitive):* either have the "returns its
   parameter" inline fold yield the **argument's** representation, or make
   the inline list compare fall back to the runtime `can_bridge` path
   instead of constant-folding `false` on mismatched element
   representations. This lives in the regression-sensitive comparison-fold
   machinery (many `list-eq*` tests), so it should be done deliberately with
   a full sweep, not as a point patch. *PLR:* sound today (false positive on
   one inline shape; never a false proof). Part of the broader **boxed vs
   unboxed list-element representation** theme (shared with how mixed lists
   are stored — see L1).

3. **`for r in f(k-1)` spurious `UnboundLocalError` — ROOT SHARPENED
   2026-06-19; deep architectural item, NOT a point fix.** Isolation
   nailed the trigger to a **recursive self-call in the loop iterable**
   (`for r in f(k-1)` inside `f`), independent of the accumulator type
   (`ret = 0` scalar reproduces it; empty-list is irrelevant). Mechanism:
   the path-sensitive unbound check asserts a **single function-scoped
   `python::f::<local>$bound` flag**, and all locals are **static
   per-function symbols** (the frontend has no per-activation call stack —
   recursion is bounded inlining). On `for r in f(k-1)`: the outer
   activation sets `ret$bound = true` (`ret = 0`), then evaluating the
   iterable **re-enters `f`**, whose entry resets `python::f::ret$bound =
   false`; the base case (`k==0`) returns **without** re-assigning `ret`,
   so the shared flag stays `false`, and the outer activation's next read
   (`ret = ret + r`) sees `false` → spurious `UnboundLocalError`. A
   non-recursive call in the iterable (`for r in g()`) is fine because `g`
   owns *different* symbols. **Why it is not a point fix:** the correct
   remedy is per-activation local storage (a real call-stack / save-restore
   of locals **and** their `$bound` flags around a self-recursive call) —
   the same static-symbol limitation also lets a recursive callee clobber a
   caller's local *values*, so this is one facet of a model-level gap, not a
   sort/list bug. It deserves its own work item (candidate: save/restore the
   recursive-self-call frame's locals+bound-flags at the call chokepoint, or
   move locals to an activation record). **Soundness caution:** the
   `$bound` flag underpins a real PLR §4.2.2 check; any change must not
   weaken genuine `UnboundLocalError` detection — validate against
   `unbound-local-*` regressions and a full sweep. Until then this stays a
   sound **false positive** (over-reports; never a false proof).

4. **`nondet_float()` NaN domain — PLR/soundness, NOT a bug to "fix".**
   `nondet_list(8, nondet_float())` then `assert elem == elem` FAILS
   because a nondet float can be `NaN` and `NaN != NaN`. This is the
   **PLR-correct** result: `NaN` is a valid `float`, so the assertion is
   genuinely not guaranteed. The corpus expects `SUCCESSFUL` only because
   ESBMC's `nondet_float` *excludes* NaN — which is **unsound** (it would
   miss NaN-triggered bugs). **HARD constraint: do not match the corpus by
   hiding NaN.** Options: (a) **accept the DIFF** (recommended; sound), or
   (b) add an opt-in, explicitly-documented-as-unsound
   `--python-nondet-float-finite` precision switch. The default stays
   NaN-inclusive.

### Complex-precision cluster — implementable plan (triaged 2026-06-19) {#complex-precision}

Two roots (`complex_binop_promotion`, `complex_builtins`,
`complex_conjugate_handler`, `complex_constructor_extended`):

1. **IEEE float-edge semantics in complex parts — DONE (2026-06-19), IEEE
   logic centralised in `util/ieee_float`.** The failures were all
   **signed-zero** subtleties (not the overflow/divzero edges first
   suspected): `copysign`, `fabs`/`abs`, complex construction of `-0.0`,
   and `0+0j` truthiness. Per review, the IEEE semantics now live in
   general, **unit-tested** helpers rather than ad-hoc frontend code:
   `util/ieee_float` gained `ieee_signbit` / `ieee_fabs` / `ieee_copysign`
   (sign = the sign **bit**, so `-0.0` is negative — a `x < 0` test is
   wrong), with dedicated Catch2 tests. The frontend now delegates:
   `math.copysign`→`ieee_copysign`, `abs(float)`→`ieee_fabs`, complex-abs
   magnitude assumes a clear sign bit, complex truthiness uses IEEE float
   equality (`-0.0 == 0.0`), and `complex()` imag accumulation preserves a
   `-0.0` addend. *Validated:* `complex_binop_promotion`, `complex_builtins`,
   `complex_conjugate_handler` all flipped DIFF→PASS; unit + full
   `regression/python` + corpus sweep at **0 regressions**; new
   `ieee-signed-zero` regression. *Note:* the C99-Annex-G complex mul/div
   overflow cases were not the failing ones and remain on the naive
   formula (sound; no failing corpus test) — a future `ieee_float` helper
   if needed.

2. **`complex()` constructor + dunder protocols — B + C DONE (2026-06-19);
   A moved to the strings plan.** `complex_constructor_extended` went from
   16 failing asserts to 2.
   - **A. `complex(<non-literal string>)`** — runtime parsing of a symbolic
     complex string (`"5+6j"`→(5,6)); a **string-solver** task, now tracked
     in the [strings plan](python-frontend-strings-plan.md#strings). The
     only remaining failures in the corpus test (the literal-string forms
     already fold).
   - **B. `__complex__`/`__float__`/`__index__` dunder dispatch — LANDED**
     (`python_converter_call_builtins.cpp`): `complex(obj)` dispatches the
     numeric dunders in CPython priority order, materialising the call into
     a temp; a wrong return type raises `TypeError` (PLR: `__complex__` must
     return complex, etc.), while unannotated/`python_value` returns fall
     through soundly (no false positive); a dunder that itself raises
     propagates.
   - **C. `TypeError` matrix — LANDED**: string-first-with-second
     (`complex("1",2)`), str/bytes second arg, and >2 positional args are
     now flagged (all genuine CPython `TypeError`s; sound to add).
   *Validated:* `complex-constructor-dunders` regression added; full
   `regression/python` + corpus sweep at 0 regressions. *Residual:* only A
   (strings plan).

> **Architectural verdict (answering "is there a whole-group root?"):**
> mostly **no** — the spurious-failure DIFFs are separate roots. The real
> whole-group levers are narrow: the mixed-numeric **compare/sort**
> promotion (item L1, covers sort/min/max) and the **complex IEEE-edge
> helper** (item C1, covers the arithmetic/abs/conjugate edges). The
> recursion-binding miss (L3) is a genuine cross-cutting root but in the
> recursion lowering, not in list code. The remaining message-format FAIL
> rows (`github_3010*`/`3015*`, `casting*-fail`, `range*-fail`) are the
> cosmetic NO-PLAN output-format item, tracked separately — not precision.

### `github_3560` family + dict/set spurious failures — triaged 2026-06-19 (each maps to a known area)

Empirical triage converted these "no plan" DIFFs into concrete fix-shapes;
they are **distinct features**, not one root:

- **`github_3560` / `github_3560_1`** = `str.split(sep, maxsplit)` on a
  **symbolic** string (`(input()+",end").split(",",1)`). This is the
  **split list-valued residual** already tracked in the
  [strings plan](python-frontend-strings-plan.md#strings) (variable-count
  result; native or a bounded list-valued lowering). Not an everything-else
  cluster — re-pointed there.
- **`dict_fromkeys` — DONE 2026-06-19 (whole-group via shared
  `build_dict_value`).** Extracted a `build_dict_value(pairs)` from
  `convert_dict` that infers element types, **de-duplicates equal constant
  keys** (PLR §6.4, last value wins), pads, and guards capacity.
  `dict.fromkeys(list-literal[, value])` builds (key, value) pairs (value
  defaults to `None`) and reuses it. The dedup also **fixed duplicate-key
  dict literals** (`{1:5,1:5,2:5}` was len 3, now 2). Flips `dict_fromkeys`;
  0 sweep regressions. New `dict-fromkeys-dedup` regression. (Residual:
  `fromkeys` over a non-list iterable, and the int/float-key-equality edge
  `{1:.., 1.0:..}`, are not deduped — uncommon.)
- **`dict_setdefault_list` — the dict-VALUE-by-reference limitation
  ([§5](#dict-byref)); NOT a standalone setdefault fix (characterized
  2026-06-19).** Empirically, *all* mutation of a list stored as a dict
  value is lost, not just via setdefault: `a={1:[]}; a[1].append(5)`,
  `v=a[1]; v.append(7)`, and `a.setdefault(1,[]).append(v)` all fail,
  because dict values are stored **by value** in the values array (a
  retrieved value is a copy; `setdefault` returns a copied temp; an empty
  `{}` even infers an `int` value type so storing a list mismatches). A
  correct fix is the dict-value-by-reference representation (mutable values
  behind a pointer, like the list-element `escaped_mutables` mechanism),
  see
  [dict-value-byref](python-frontend-dict-value-byref-plan.md): Option 2
  (lvalue value slots) is **LANDED for int keys** (`a[k].append`,
  `setdefault(k,d).append` now mutate in place, 0 regressions). The
  remaining `dict_setdefault_list` failure is the empty-`{}` value-typing
  residual (`a={}` infers an int value type); string-keyed dict-value
  mutation and `v=a[k]` extraction-aliasing are documented residuals.
- **`set_from_param` — NOT a set fix; multi-call constant-fold conflation
  (characterized 2026-06-19).** `set(<str>)` itself works (constant-folds a
  string to a unique-char bitmap; `len(set("abc"))==3`, and a single
  `f(s)=len(set(s.lower()))` call folds). The failure is that
  `set_from_param` calls `f` **twice** with different constant args
  (`f("aAa")`, `f("abc")`): the body is converted once, so arg-side
  constant propagation (`string_constants[s]`) can't hold both — both calls
  read one conflated value. This is a general
  function-called-with-different-constants constant-fold limitation (the
  monomorphisation/clone-per-call-site machinery doesn't cover the
  string-constant-fold case), not a `set` bug. Deeper than a point fix.

*Net:* one (split) folds into the strings plan; `dict.fromkeys` is an
implementable standalone feature; `setdefault`-returns-mutable is dict-by-ref;
`set(str)` is set-from-iterable. No new architectural root.

---

## 10. Attribute / descriptor protocol residuals (PLR §3.3.2)  {#descriptors}

**Status: PARTIAL.** `@property` on all receiver shapes (incl. inherited
via MRO), `__getattr__` fallback, and stateless custom-descriptor `__get__`
are landed.

**Residuals (KNOWNBUG, sound):**

- **Non-data-descriptor (method) shadowing** (`method-shadow`):
  an instance attribute shadowing a method. **RESOLVED (2026-06-22,
  `7dd6429f0d`).** A method-shadow attr (`c.m = v` for a method `m`) gets a
  `python_value` storage field + a runtime `__shadow_m` flag; a bare read
  `c.m` dispatches via `if(__shadow_m) instance.m else <nondet>` (the
  unshadowed fallback is a sound nondet over-approximation; method CALLS
  `c.m()` are unaffected). Reuses the existing class-level-data-attr
  shadow-fallback ternary, extended to non-data descriptors. 0 sweep
  regressions.
- **Custom-descriptor `__set__` + stateful `__get__`. DONE (2026-06-22).**
  `c.x = v` routes through `__set__` (`emit_descriptor_set`), `c.x` through
  `__get__`; the instance is boxed as a CLASS `python_value` so `obj._v`
  aliases it, and a 1a-bis re-pass (third condition) re-converts descriptor
  methods after the field-owning class is registered, so forward-defined
  owners resolve. Stateful descriptors verify (`data-descriptor-stateful`,
  `data-descriptor-state-forward`, `data-descriptor-set`).

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
2. **Shadowing** — **DONE (2026-06-22, `7dd6429f0d`).** A method-shadow attr
   gets a `python_value` field + `__shadow_m` flag; bare reads dispatch via
   `if(__shadow_m) instance.m else <nondet>`. Fixed `method-shadow` (was
   KNOWNBUG). The static-discovery phase still skips method-named attrs for
   *field* declaration of non-shadow uses; the shadow path declares the field
   when a `c.m = v` (m a method) is discovered.

   **Whole-group primitive — bound-method-as-value boxing — LANDED
   (2026-06-22).** A bound method is now boxed as a runtime CLOSURE
   `python_value` capturing `self` (`box_bound_method`; see
   [§12](#higher-order)), so it flows through containers / conditionals /
   returns and dispatches self-first. This is the shared primitive for the
   group: it can now make (a) the unshadowed shadow-ternary fallback EXACT
   (replace the nondet with `box_bound_method(C::m, self)` — a small,
   optional precision follow-up; the nondet is already sound), and (c) the
   runtime-`__dict__` method fallback (phase 3). Higher-order bound-method
   values (b) are done.
3. **Custom data descriptors** — `__set__` / stateful `__get__`. **DONE
   (2026-06-22).** `c.x = v` routes through `emit_descriptor_set` →
   `__set__`; the instance is boxed as a CLASS `python_value`
   (`coerce_to_typed_slot`, sets `__class_tag`) so `obj._v` aliases the
   instance; a stateful descriptor (`__set__` stores `obj._v`, `__get__`
   reads it back) verifies (`data-descriptor-stateful`,
   `data-descriptor-state-forward`).

   **Whole-group root + fix — LANDED.** `obj.attr` on a `python_value`
   resolves at *conversion time* against `class_types`; a
   **forward-referenced** class/field (defined later in the file) used to
   bake a nondet — a *group* root governing any `python_value` attribute
   access to a later-defined class/field, not just descriptors. Fixed by a
   third **1a-bis re-pass condition**: re-convert a class if any method
   accesses `<p>.attr` on a non-self UNANNOTATED parameter (a
   `python_value`), so its body re-converts after the full 1a loop has
   registered every class struct (incl. discovered dynamic attrs). The
   re-pass is idempotent; 0 sweep regressions, +0.7% wall time. Closes the
   common forward-defined stateful descriptor pattern AND non-descriptor
   forward-referenced field access (`forward-ref-field-access`).

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

**Bound method as a runtime value — LANDED (2026-06-22).** A bare method
read `c.f` (not immediately called, not an alias target) is boxed as a
runtime bound-method value (a CLOSURE `python_value` capturing `self`,
reusing the fat-closure runtime; `box_bound_method` +
`bound_method_closures` self-first dispatch). This lets a bound method
flow through a **container** (`handlers=[c.f]; handlers[0]()`), a
**conditional** (`m = c.f if cond else c.g; m()`), and a **function
return** (`pick(c)()`) and be dispatched later — each previously a "no body
for callee" false positive. The direct `m = obj.f; m()` case keeps using
the (cheaper) conversion-time alias. Bound methods with args dispatch
self-first; negatives correctly FAIL. Regression `bound-method-value`; 0
sweep regressions. This is the shared bound-method-as-value primitive
([§10](#descriptors)) that also enables an exact method-shadow fallback
and the runtime-`__dict__` method fallback.

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
**Await-result binding — RESOLVED (verified 2026-06-19).** The prior live
gap (binding an `await` / async-call result to a variable left the target
**unbound** — `v = await f(4)` raising a spurious `UnboundLocalError`) no
longer reproduces on the current binary. All forms now bind correctly and
verify: inline `assert (await inc(4)) == 5`, assignment `v = await inc(4)`,
reassignment `v = await inc(v)`, and `await` inside an `async def` driven by
`asyncio.run(main())`. Locked in by the `async-await-assign` regression
test. Single-task `await` chains are sound and exact.

**Plan (remaining).**

1. ~~Fix `await`/async-call result binding~~ — **DONE** (see above).
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

## 15. Decorator application (PLR §8.7)  {#decorators}

**Status: DONE (2026-06-30, commit `27bb327b26`).** Both decorator false proofs
`dec_not_callable` and `dec_wrong_arity` are closed. Implementation notes (where
it diverged from the spike plan) are in the **OUTCOME** block at the end of this
section.

**PLR semantics.** `@dec def f(x): …` lowers to `def f(x): …; f = dec(f)`,
evaluated at def-time (stacked decorators apply bottom-up: `@d1 @d2 def f` →
`f = d1(d2(f))`). `dec` must be callable (else `TypeError` at def-time); the
result rebinds `f`, and the *returned* callable's signature governs subsequent
calls.

**Spike (confirmed by code read + cbmc).** The frontend ALREADY partially applies
general decorators: `convert_function_def` has a `user_decorators` application
loop (`python_converter_defs.cpp:1932`–~2062) that, for each decorator, resolves
the decorator value and registers a `function_aliases` entry so calls to `f`
dispatch through the wrapper. Two precise gaps:
- **`dec_not_callable`** — at `:1967`, `if(dec_expr.is_nil() || dec_expr.type().id()
  != ID_code) continue;` *silently skips* a non-callable decorator value (e.g. an
  `int`), emitting no TypeError.
- **`dec_wrong_arity`** — the wrapper lookup at `:2054` builds the nested-function
  symbol as `"python::" + inner_name`, but a function nested in `dec` is qualified
  `python::dec::<inner>`; the lookup misses, the alias is not registered, and the
  call bypasses the wrapper (dispatching to the original `f`, whose arity matches)
  — so the wrapper's arity is never enforced.

Both confirmed: `dec_not_callable`/`dec_wrong_arity` verify SUCCESSFUL today; the
cited code sites exist; `function_max_positional`/`function_vararg_index` already
back `validate_call_signature`.

**Phase 1 — def-time callability check (`dec_not_callable`).** At `:1967`, when
`dec_expr` is non-nil and its type is neither `ID_code` nor `python_value`
(Any) — i.e. a *provably non-callable* concrete value (int/float/str/list/…) —
emit `emit_conditional_exception(true_exprt{}, "TypeError")` instead of silently
`continue`-ing. The `FunctionDef` statement flushes pending checks, so the
TypeError fires at the def site (CPython's def-time semantics).
*Soundness gating (no FP):* skip a `nil` decorator (unresolved import/forward-ref
— cannot prove non-callable) and a `python_value`/Any decorator (might be
callable); functions/classes/lambdas are `ID_code` and fall through unflagged;
`@staticmethod`/`@property`/`@icontract`/`@c_intrinsic` are filtered out before
this loop. ~10 lines, one site.

**Phase 2 — wrapper-arity enforcement (`dec_wrong_arity`).** Fix the wrapper
lookup at `:2054` to use the decorator's qualified name (`dec_name + "::" +
inner_name`) with a fallback to the bare name. Once the alias points at the real
wrapper symbol, the EXISTING `validate_call_signature` enforces the wrapper's
arity at the call site (the wrapper is undecorated, so it is in
`function_signature_checkable` with `function_max_positional = 0`) — *no further
code change* for the check itself. *Soundness gating (no FP):* a wrapper with
`*args` has a `function_vararg_index` entry → arity check correctly skipped; a
wrapper whose arity matches the call is unaffected.

**Acceptance criteria.** Gates: `dec-not-callable-knownbug` → CORE FAILED (Phase
1); `dec-wrong-arity-knownbug` → CORE FAILED (Phase 2). No-FP: a parametric
decorator (`@deco(arg)` / `@functools.wraps(fn)`), an Any-typed decorator
(`d: Any; @d`), a `*args` wrapper, `@staticmethod`/`@classmethod`/`@property`,
and the existing `python-decorator-varargs` / `python-decorator-inside-function`
tests all stay SUCCESSFUL. Per phase: suite green, sweep 2719/0-reg, oracle
0-NEW. Estimated ~20 lines + the no-FP regression tests.

**OUTCOME (DONE — what actually shipped).** Two divergences from the spike plan:

1. *Phase 1 emission point.* `emit_conditional_exception` at `:1967` did **not**
   fire for a module-level `@d def f`: a module-level FunctionDef is registered
   in an earlier pass and its `convert_function_def` result (`dec_block`) is
   **not** added to `__main__` (`convert_module_body` special-cases FunctionDef
   and only evaluates defaults), and `pending_checks` are discarded at the def
   site. So the def-time TypeError is emitted at **two** points: (a) module-level
   defs — directly in `convert_module_body`'s FunctionDef branch, with an
   explicit `uncaught exception` assert (the per-statement loop skips FunctionDef
   for that assert); (b) nested defs — into the returned `dec_block` in
   `convert_function_def` (those DO reach their enclosing body via
   `convert_statement`). Gating tightened beyond the plan: only a **bare
   `@Name`** decorator is checked (Call/Attribute factory & library decorator
   forms are skipped — this is what keeps `@icontract.require(...)` from
   false-positiving), and a user-class-instance decorator is callable iff its MRO
   defines `__call__` (`concrete_class_lacks_dunder`).
2. *Phase 2 alias collision.* The qualified wrapper lookup
   (`python::<dec>::<inner>`) was necessary but not sufficient: the oracle witness
   names the decorator parameter `f` — the same as the decorated function — and
   the fn-param binding `function_aliases["python::f"] = original` then clobbered
   the decorated-function→wrapper alias (same key), masking the wrong-arity call.
   Fixed with a guard that skips the global bare-name fn-param binding when it
   equals the decorated function's symbol id (the wrapper-scoped and
   param-qualified bindings still resolve `fn` inside the wrapper body).

Validated: regression/python suite green; sweep 2719 PASS / 0 regressions; oracle
false proofs **3 → 1** (only `gen_send_before_start` remains), 0 new. CORE
lock-in tests: `dec-not-callable-typeerror`, `dec-wrong-arity-typeerror` (uses
the colliding-param witness), `dec-callable-nofp` (identity / `*args` / callable
instance — all SUCCESSFUL).

---

## Tracking conventions

When picking up an item: update its **Status** line (add the in-progress
commit ref), land a focused regression test, re-run the three suites and
the ESBMC sweep, and on completion either mark the residual closed or move
the item out of this doc (history stays in git). If an item splits, add the
refined sub-items with their own fix shapes.
