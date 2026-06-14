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

> **Refreshed status (2026-06-14).** **P0 (soundness) is empty** — all known
> false proofs are closed (return-type `None`-erasure; the whole Any-typed
> container by-reference mutation family incl. methods/sets/`__tag`-dispatch;
> dict-changed-size-during-iteration). **P1 (native robustness) is effectively
> done** — the dict-by-ref-mutation crash is gone; only a latent
> `smt_string`-in-byte-op gap with no corpus instance remains. So the live
> frontier is P2+. **Recommended next order:**
> 1. **P2 SMT-track native method reach** — wire the remaining native `str.*`
>    ops (`upper`/`lower`/`casefold`/`title`, `split`, `count`/`rfind`/`rindex`,
>    `str(float)`, `repeat`, `strip(chars)`). Incremental, contained, low-risk,
>    each a small lowering; closes real precision gaps now nondet under native.
>    Best ROI.
> 2. **String-refinement performance cliff** (§8) — the refined default times
>    out on string-keyed dict scans / value-updates and `str.in_re`+`len()` on a
>    shared symbolic subject. Now the dominant refined-backend limitation (it is
>    what keeps string-keyed dict code "correct but slow" on the default path,
>    e.g. the residual on `github_3647_9` string keys). High impact, but
>    hard/research-grade (refinement-solver internals).
> 3. **P3 regex reach** (literal-symbolic patterns, `re.sub`, groups,
>    `re.split`, flags) — builds on the native regex path; medium effort.
> 4. **P2 refined-track parity** (membership convergence, ordering,
>    producing-op precision) — deep existential-witness/solver work; native
>    already covers these, so lower urgency.
> 5. **P4 JBMC native-SMT-string spike** and **P5 maintenance/re-checks**.


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
  - still nondet (sound) under native: symbolic `count` and backward
    `rfind`/`rindex` (no SMT `str.last_indexof`, no count primitive — bounded
    `str.indexof` loops possible but perf-heavy); `title`; `split` (list-valued);
    `str(float)` (no SMT float→string); `repeat` with symbolic `n` (nonlinear);
    `strip(chars)` (explicit fill-set).

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
- **Still to do (precision):** literal-symbolic patterns (segment list +
  `str.to_re` holes; the anchors are now soundly modelled so the embedded-anchor
  bail is the remaining caveat); group extraction (no SMT capture-group support —
  needs a bespoke bounded encoding); `re.split`/`findall` (list-valued);
  precise compilation flags.
- **Compilation flags — sound floor LANDED (2026-06-14); precision TODO.**
  Module-level `re.search`/`match`/`fullmatch` now bail to nondet when
  `flags != 0` (was unsound: the flag was dropped and the flag-free decision
  used — e.g. `re.search("abc","ABC",re.IGNORECASE)` proved no-match). Remaining:
  precise IGNORECASE (ASCII case-fold the pattern) / DOTALL (`.`→allchar) via a
  translator flags mode, and flags on **compiled** patterns. The compiled case
  is blocked on a propagation gap — a `re.compile(p, flags)` Pattern's `self.flags`
  int field reads as nondet (its string `pattern` field propagates fine), so
  threading `self.flags` either drops it (unsound) or makes every compiled match
  nondet (regresses no-flag precision). A full precise-flags attempt (translator
  flags param + intrinsic flags operand + module-constant folding) was
  prototyped and reverted pending that fix and an intrinsic-arity-consistency
  cleanup.
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
2. **Library `Match`/`None` result not tied to the intrinsic.** The `re`
   stub calls `__cbmc_re_{match,search,fullmatch}` but always returns
   `Match()` (a deliberate choice so `re.match(...) is not None` stays
   provable under the nondet default). Even with gap 1 fixed, a flag-gated
   `--python-strict-re-result` is needed so a matched call returns `Match()`
   and a proven no-match returns `None`, without regressing the existing
   `re*` tests that rely on always-`Match()`. Smaller than gap 1, and only
   meaningful once gap 1 lands.

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
