\file
# (a') Phase 2 — back-end abstraction design

Status: **design draft**, May 2026. This is the Phase 2
deliverable of the Python-string representation refactor
documented in the strings plan (`../python-frontend-plans.md` §3). The
Phase 1 inventory (the Phase 1 inventory (folded into `../python-frontend-plans.md` §3)) listed
the ~50 front-end sites that reach into the refined-string
struct; this document specifies the back-end interface those
sites will compile through so that each back-end can pick its
own representation.

Sign-off on this design is the gate to starting Phase 3 (the
first frontend refactor PR).

## Problem statement

The Python front-end today constructs a refined-string struct
``{length: int64, data: char*}`` at every string-production site
and reads ``.length`` / ``.data`` at every consumer site.
Consequences:

* Backend choice leaks into frontend. SMT backends that natively
  support SMT-LIB strings (notably CVC5) can't see a string as a
  ``String``; they get a bitvector-struct encoding with
  axiom-driven ``cprover_string_*_func`` lookups.
* Wave 2's SMT-regex precision gets stuck behind a symbolic
  subject. The smt2_conv interception can substitute a
  *literal* subject into ``str.in_re``, but symbolic subjects
  propagate as refined-string structs that CVC5 can't reduce.
* Future String operations (e.g. Unicode-aware substring,
  find-from, replace) need axioms on two backends plus frontend
  plumbing.

The representation choice should be the **back-end's**
decision, not the front-end's.

## Non-goals

This document does *not*:

* Change JBMC. The refined-string back-end stays, and it
  continues to be JBMC's default.
* Change C/C++ front-ends. They stay on the C array-of-char
  model.
* Propose a universal SMT-String canonicalisation. Each
  back-end picks its representation; only the frontend-visible
  interface is standardised.

## Interface

Each back-end exposes:

### 1. A ``python_string_representationt`` type handle

An opaque ``typet`` that represents "a Python string value" for
this back-end. Concretely:

* **Refine-strings** returns the existing
  ``python_string_type()`` (``struct_typet`` with ``length`` and
  ``data``). Same shape as today; JBMC compatibility preserved.
* **SMT-string-capable back-ends** return a new
  ``smt_string_typet`` that ``convert_type`` in ``smt2_conv``
  maps to SMT-LIB ``String``.
* **MiniSat/CaDiCal** (no string theory) returns the
  refined-string struct, same as refine-strings.

The frontend calls a single dispatch point:

```cpp
typet python_string_type() {
  return back_end().python_string_representation();
}
```

This hides the choice. Existing front-end code that currently
calls ``python_string_type()`` continues to compile; the type it
gets back changes with the back-end.

### 2. A literal constructor

```cpp
exprt python_string_literal(const std::string &content);
```

Lowers to:
* **Refine-strings**: the current
  ``struct_exprt{length=const, data=address_of(array[0])}``
  wrapped in the existing static-storage machinery
  (``build_string_literal``).
* **SMT-string**: an ``smt_string_constant_exprt`` that
  ``convert_expr`` maps to the SMT literal ``"…"``.

### 3. Per-intrinsic lowering

For each ``cprover_string_*_func`` intrinsic, each back-end
provides a lowering. The frontend never constructs the struct
directly; it always emits the intrinsic, and the back-end
decides how to handle it.

The intrinsic set that Phase 3 will use:

| Intrinsic | Refine-strings | SMT-string |
| --- | --- | --- |
| ``_literal_func(s)`` | ``{len, addr(s)}`` via ``build_string_literal`` | ``"s"`` |
| ``_length_func(s)`` | ``s.length`` | ``(str.len s)`` |
| ``_equal_func(a, b)`` | axiom-driven | ``(= a b)`` |
| ``_concat_func(a, b)`` | axiom-driven | ``(str.++ a b)`` |
| ``_char_at_func(s, i)`` | axiom-driven | ``(str.at s i)`` |
| ``_is_prefix_func(s, p)`` | axiom-driven | ``(str.prefixof p s)`` |
| ``_is_suffix_func(s, p)`` | axiom-driven | ``(str.suffixof p s)`` |
| ``_contains_func(s, p)`` | axiom-driven | ``(str.contains s p)`` |
| ``_index_of_func(s, p)`` | axiom-driven | ``(str.indexof s p 0)`` |
| ``_index_of_from_func(s, p, i)`` | new axiom | ``(str.indexof s p i)`` |
| ``_last_index_of_func(s, p)`` | axiom-driven | (emulate via reverse) |
| ``_to_upper_case_func(s)`` | axiom-driven | ``(str.to_upper s)`` (SMT-LIB 2.7) |
| ``_to_lower_case_func(s)`` | axiom-driven | ``(str.to_lower s)`` (SMT-LIB 2.7) |
| ``_substring_func(s, a, b)`` | new axiom | ``(str.substr s a (- b a))`` |
| ``_repeat_func(s, n)`` | new axiom | emulate via recursion / unroll |
| ``_compare_func(a, b)`` | new axiom | ``(ite (str.< a b) -1 (ite (= a b) 0 1))`` |
| ``_replace_func(s, old, new)`` | new axiom | ``(str.replace s old new)`` |
| ``_strip_func(s)`` | new axiom | regex-based ``(str.replace_re …)`` |
| ``_match_func(p, s)`` / ``_search_func`` / ``_fullmatch_func`` | new axiom | ``(str.in_re s P)`` via Wave-2 translator |

New intrinsics (nine in total) need:
1. New ``ID_cprover_string_*_func`` in ``util/irep_ids.def``.
2. Axiom in ``solvers/strings/string_constraint_generator_*``.
3. Lowering in ``solvers/smt2/smt2_conv.cpp`` (SMT path) and/or
   the standard refine-strings dispatch.

### 4. Selector: which back-end is in use?

``python_string_type()`` and ``python_string_literal()`` need a
single source of truth for the active back-end. Options:

* **Compile-time** via a solver flag on ``config``. Decision
  made at ``cbmc_parse_options`` time; stored in
  ``python_languaget`` so the converter reads it off a single
  pointer. Prefer this for simplicity — the back-end choice
  is fixed per run, so static dispatch is sufficient.
* **Runtime polymorphism** via a ``python_string_back_endt``
  interface. More flexible but overkill for our needs.

Recommended: compile-time dispatch. A single
``enum class python_string_kind { refined, smt_string };`` read
from ``config`` at conversion time, with two inline helpers
``python_string_type_refined()`` and ``python_string_type_smt()``
that produce the respective ``typet``. The selector chooses.

### 5. Per-backend discovery

SMT-string support is conditional on the solver accepting the
theory. CVC5 and Z3 support SMT-LIB strings; MiniSat / CaDiCaL
don't. The selector falls back to ``refined`` unless:

* ``--cvc5`` is passed AND a new opt-in flag
  ``--python-smt-strings`` is set, OR
* ``--z3`` is passed AND the same opt-in flag is set.

Default remains refined strings for compatibility. The opt-in
flag is there so we don't break existing
``--cvc5``-using users during the rollout.

## Frontend impact

The ``python_convertert`` changes are:

* **Single dispatch helper**: ``python_string_type()`` already
  exists; it starts returning the back-end's choice.
* **``build_string_literal`` becomes a dispatch**. For the
  refined-string back-end it continues to emit the static-
  lifetime symbol as today. For the SMT-string back-end it
  emits an ``smt_string_constant_exprt``.
* **``build_string_struct`` is retired**. The 33 callers move to
  ``build_string_literal``. (Already planned for Phase 3 step 1
  in the inventory.)
* **Every ``.length`` and ``.data`` member access**: replaced
  with an intrinsic call. The back-end's lowering decides the
  operational semantics.

The **interface boundary exceptions** keep struct access:
* ``@c_intrinsic`` str → ``char *`` marshalling. The Python
  string has to become a ``char *`` for the C callee, so we
  extract a ``.data`` pointer or convert from the SMT String.
  Refine-strings does it the same as today; SMT-string requires
  a ``cprover_string_to_char_star_func`` intrinsic with a
  per-backend implementation. New Phase 3.5 item.

## Backend impact

* ``solvers/flattening/boolbv.cpp``: when the type is
  ``smt_string_typet``, bail out — it's the SMT path's
  responsibility, not bitvector.
* ``solvers/smt2/smt2_conv.cpp``:
  - ``convert_type`` maps ``smt_string_typet`` to ``String``.
  - ``convert_expr`` recognises the new intrinsics and emits
    the appropriate SMT-LIB term.
  - String literals emit ``"…"`` directly when the type
    is SMT-string.
* ``solvers/strings/string_constraint_generator_main.cpp``:
  gains ~9 new axiom handlers for the intrinsics listed above
  that don't currently exist.
* ``solvers/strings/string_refinement*``: the existing
  refinement loop is unchanged; it just gets more intrinsics to
  resolve.

## Phase 3 ordering revision

The Phase 1 inventory proposed five PRs; revisiting with the
back-end abstraction in mind:

**PR 1 — Infrastructure (no behaviour change)**
* Introduce the ``python_string_kind`` selector in
  ``python_languaget``.
* Add the opt-in flag ``--python-smt-strings`` (defaults off).
* Make ``python_string_type()`` a dispatch; refined-string
  branch returns today's shape.
* Add the nine new ``ID_cprover_string_*_func`` ids and stub
  axiom handlers (returning nondet with no axioms).
* No frontend call-site migration yet.

**PR 2 — Literal producer migration**
* Replace all 33 ``build_string_struct`` calls with
  ``python_string_literal`` dispatches.
* Retire ``build_string_struct`` (static free function).
* Refine-strings path unchanged; SMT-string path emits
  ``smt_string_constant_exprt``.

**PR 3 — Length + is_empty + char_at**
* Replace ``.length`` reads with ``cprover_string_length_func``.
* Replace truthiness with ``cprover_string_is_empty_func``.
* Replace ``.data[i]`` reads with ``cprover_string_char_at_func``.
* Truth-value testing of strings now goes through the intrinsic
  set.

**PR 4 — Producers (concat / repeat / upper / lower)**
* Migrate string concat and multiplication to
  ``cprover_string_concat_func`` and the new
  ``cprover_string_repeat_func``.
* Migrate upper/lower if not already intrinsic-based.

**PR 5 — Ordering / comparison**
* New ``cprover_string_compare_func`` replaces the byte-0
  comparison in ``<``, ``<=``, ``>``, ``>=``.

**PR 6 — Substring, slicing, replace, find-from, strip**
* Each gets its new intrinsic and front-end migration.

**PR 7 — Retire the @c_intrinsic struct access**
* New ``cprover_string_to_char_star_func``.
* Back-end implementations (refine-strings returns ``.data``
  as today; SMT-string converts to C buffer).

**PR 8 — Turn on SMT-string by default for --cvc5**
* Flip the opt-in default; remove any remaining refined-string
  shim on the SMT path.
* Re-run benchmarks; expect CLEAN improvements on regex-heavy
  code and no regressions on non-regex code.

Phase 4 would be any clean-up + SMT-LIB 2.7 string-case
functions once CVC5 supports them broadly.

## Risks

1. **JBMC regressions surface** via shared code paths. Every PR
   runs JBMC's regression suite before landing.
2. **New axiom correctness**. The nine new intrinsics each
   ship with a regression test asserting both satisfiability
   and unsatisfiability of representative queries.
3. **SMT-string solver performance** on simple programs. We'll
   benchmark before flipping the default in PR 8. If CVC5 on
   SMT-strings is slower than CVC5 on refined-strings for the
   common case, we keep refined-strings as the default and let
   users opt in.
4. **Unicode semantics mismatch**. Python strings are
   code-point-oriented; SMT-LIB String is Unicode; refined-string
   is byte-oriented. Phase 3 should establish a small set of
   representative regression tests (unicode-aware slicing, find,
   iteration) and fix where necessary. Expect to discover edge
   cases; the new intrinsics give us a place to encode them.

## Open question

Should ``cprover_string_to_char_star_func`` be its own intrinsic
or a back-end-specific shim? If Python is the only front-end
that needs this, a per-call-site converter (not an intrinsic)
may be cleaner. The trade-off: an intrinsic standardises the
interface but adds one more axiom; a direct converter is leaner
but doesn't compose. **Preferred: intrinsic**, for composability
and so the axiom set is the single source of truth.

## Sign-off

This document represents the Phase 2 design deliverable.
Approval is the gate to PR 1 (infrastructure). If any of the
open questions or design choices need revision, that revision
happens in this document before any code lands.

## Spike findings (2026-06-09) — github_3090_4, multi-backend diagnosis

A spike on `github_3090_4` (`s = chr(i1)+chr(i2)+chr(i3); assert s ==
"foo"` under `__ESBMC_assume(i1==102)` etc.) precisely root-caused why
symbolic-string **content** equality fails today, across **every**
back-end. The front-end already emits the right intrinsics
(`cprover_string_concat_func`, `cprover_string_equal_func`); the gap is
purely that no back-end solves them for *symbolic* content of the Python
`{length, data: char*}` representation:

* **Default back-end (the sweep).** `cbmc_parse_options` auto-enables
  `--refine-strings` (when not `--z3/--smt2/--cvc5`), so the sweep *does*
  run the string refinement — but it **does not engage** for Python
  strings. The refinement's `add_axioms_for_equals` expects
  `refined_string_typet` operands with a **content array** (`char[]`),
  whereas the Python string carries a **content pointer** (`char*` =
  `address_of(array[0])`). Confirmed empirically: `--refine-strings` flips
  *none* of `github_3090_4`, `string-rfind-nondet`, `string-index-nondet`,
  `re2`.
* **SMT2 back-ends (`--z3`, `--cvc5`).** `smt2_conv.cpp` lowers
  `cprover_string_equal_func` to **structural** equality `(= s1 s2)` —
  comparing the `{length, data-pointer}` structs. `chr(i)` and `"f"` live
  in different local arrays, so the pointers differ → "not equal" →
  spurious FAIL (sound but imprecise, as its comment states). Both `--z3`
  and `--cvc5` FAIL `github_3090_4`.
* **Symex constant-propagation.** `constant_propagate_string_concat` (and
  friends) only fire for **constant** strings (`try_evaluate_constant_string`
  bails on a symbolic char), which is why *literal* concatenation
  (`"f"+"o"+"o" == "foo"`) verifies but `chr(<symbolic>)` does not.

Feasibility of the underlying solving is **not** in doubt — the residual
constraint is trivially `i1==102 ∧ i2==111 ∧ i3==111`. The blocker is
entirely the **representation/plumbing**: the `char*`-vs-`char[]` mismatch
(refinement) and the structural-vs-content lowering (SMT2).

**Why a point-fix is not clean.** A narrow content-equality rewrite would
have to live in shared core code (`simplify_expr` / `smt2_conv` /
`boolbv`) used by all of CBMC and JBMC, handle the `char*`→content
indirection and symbolic element-wise comparison with a length bound, and
do so without regressing the constant-only fast paths — i.e. it
re-implements a slice of the refinement against the wrong representation.
That is fragile and does not generalise to the other ~19 string/`re`
cluster DIFFs.

**Go / no-go: GO on the representation refactor (PR 1 → …).** The spike
confirms this is the single architectural root for the whole string/`re`
precision cluster: once a string is the back-end's chosen type (SMT-LIB
`String` on CVC5/Z3, proper `refined_string_typet` on refine-strings) and
every `.length`/`.data` access is an intrinsic, content equality (and
concat/contains/find/slice/regex) is solved natively per back-end.
Recommended first concrete step is **PR 1** (selector + `--python-smt-strings`
opt-in + the new intrinsic ids/stubs, no behaviour change), then **PR 2**
(literal producer migration), validating end-to-end on `github_3090_4`
with `--cvc5 --python-smt-strings` before the broader call-site migration.
Solver impact is expected positive on CVC5 (`str.=`/`str.++` are native and
the refinement axiom path is already proven at scale in JBMC); the cost is
engineering scope, not solver capability.


## Experiment 2 (2026-06-10) — array_pool content-association; and the loop limitation

A follow-up experiment **corrected** the spike's representation claim and
surfaced a decisive design finding.

**Correction: the `char*` representation is NOT the blocker.** JBMC's
refined string is *also* `{length, content}` with `content` a **`char*`**
(`refined_string_exprt` stores `to_pointer_type(content)`; JBMC builds
`content = a->data`), and JBMC proves symbolic strings fine. The real
difference is that `add_axioms_for_equals` reads content via
`get_string_expr(array_pool, arg)`, and the **`array_pool` must associate
the content pointer with a backing char array**. JBMC establishes that
association (`code_assign_java_string_to_string_expr` → `checked_dereference`
+ `replace_char_array`); the Python front-end does **not** for leaf strings
(`chr`, etc.), so `array_pool.find` invents a *fresh unconstrained* array
and the equality is unprovable.

**Experiment: associate `chr`'s content array.** Materialising `chr(i)`'s
content into a symbol array and emitting
`cprover_associate_array_to_pointer` / `..._length_to_array` for it made
`chr(i) == "f"` **and** `github_3090_4` / `github_3090_5` (the concat
chain) **verify under the default `--refine-strings` backend** — and
soundly (`chr(<nondet>) == "f"` and `chr(103) == "f"` still FAIL). So the
diagnosis is confirmed: the missing **association**, not the
representation, is the blocker, and the fix lives in the **front-end**
(reusing the existing refinement) — *not* a new SMT-string backend.

**But the array_pool association has a fundamental loop limitation.** The
same experiment **crashed** `github_3130_fail`
(`for i in range(97,100): assert chr(i) not in s`) at
`array_pool.cpp:175` — *"should not associate two arrays to the same
pointer"*. A `chr` in a loop is converted once; symex unrolls it, so the
single static content pointer (a fixed symbol's address, version-
independent) is associated **every iteration**, while the content array
symbol is **SSA-versioned** — i.e. the *same pointer* legitimately maps to
*different arrays* per unwind, which is exactly the (correct) invariant the
pool enforces. An idempotent relaxation does **not** help (the SSA versions
are genuinely different arrays). Making it work would require **fresh
per-execution storage** for every `chr` (distinct pointer per dynamic call,
i.e. allocation), which is heavy. The experiment was reverted (can't ship a
crash); tree is clean.

### Backlog: array_pool association vs. symex content-pointer dereference

This is direct evidence for the open question of whether `array_pool`
association is the right mechanism, or whether **symex should also
dereference string content pointers** before the back-end.

* **array_pool association (today):** keys content by *pointer identity*.
  Works for straight-line leaf strings, but a fixed-address leaf string
  re-built in a loop maps one pointer to many SSA arrays → invariant
  crash. Robust use would require per-call fresh allocation.
* **symex content-pointer deref (alternative):** have symex resolve the
  content pointer to the actual (SSA-versioned) char array at each use, and
  feed *that* to the refinement, instead of relying on a per-pointer pool
  association. Each loop unwind reads the current content naturally — no
  association, no re-association crash — and straight-line cases still
  work. This sidesteps the pointer-identity constraint entirely and is the
  more promising route; the cost is teaching the string solver / front-end
  to obtain content via the dereferenced array rather than `array_pool`.

Recommendation for the string-precision work: prefer the **symex-deref**
integration over broadly emitting `array_pool` associations from the
front-end, and re-evaluate the SMT-string backend (`--python-smt-strings`)
as the orthogonal CVC5/Z3 precision option. The `chr`-association
experiment proves the refinement *can* solve these once it sees the real
content — the remaining design choice is purely *how* to deliver that
content (pointer-association vs. dereference), and the loop crash makes the
case for dereference.


### Update (2026-06-10) — corrected conclusion + symex-deref feasibility

Two further investigations refine the backlog note above.

**(a) How JBMC survives the loop — and why our crash was self-inflicted.**
JBMC's refined-string `content` is a *declared `char*` variable*
(`cprover_string_content`) **assigned `= a->data` per execution**
(`decl_string_expr` adds `DECL`; `code_assign_java_string_to_string_expr`
adds the assignment), and each loop iteration constructs a **distinct heap
object**, so `a->data` is a genuinely **different pointer value** per
iteration and `cprover_string_content` gets a **fresh SSA version**. The
`array_pool`'s one-array-per-pointer invariant is therefore satisfied —
**JBMC does not crash**. Our experiment crashed only because `chr` used a
**single static array** with a constant, version-independent
`address_of(...)` pointer that every unwind re-associated. So the crash is
an artifact of *static, shared content storage*, **not** a fundamental
`array_pool` flaw. **Corrected bottleneck: per-execution content storage**,
not the association mechanism.

**(b) `array_pool.find` already has a crash-free fast path.** For a content
pointer of the *syntactic* form `address_of(index(<ID_array>, 0))`,
`array_poolt::find` (`array_pool.cpp`) **returns that array directly and
does NOT insert into `arrays_of_pointers`** — so it never trips the
invariant, even in loops, and it handles arrays with *symbolic elements*
(`[cast(i,uint8)]`). It falls through to a *fresh unconstrained* array only
when the pointer is a `member`/`symbol` (e.g. `s.data` after the string is
stored in a variable). **So the real gap is variable indirection hiding the
literal array behind a member pointer** — not the pointer representation,
and not loops per se.

**Storage options (if staying on the association route):**
* *Fresh per-construction allocation* (JBMC-style): each string producer
  allocates its own content (distinct pointer per dynamic execution). Most
  faithful; costs an allocation per produced string (strings are already
  pointer-backed, so this is storage-shape, not a new indirection).
* *Pooled/arena per call site*: cheaper but reintroduces aliasing across
  iterations unless versioned.
* *Escape-only*: only allocate fresh storage for strings that outlive a
  loop iteration; keep the static-array shortcut for within-iteration
  temporaries. Smallest change, but needs an escape analysis.

**(c) symex content-pointer dereference — FEASIBLE.** Verified that symex
can resolve a content pointer to its backing array:
`value_set_dereferencet` (`value_set_dereference.cpp`, `try_add_offset_to_
indices`) explicitly handles `*(p + i)` for **symbolic `i`**, producing
`object[i]` (guarded across multiple value-set targets) — and
`symex_dereference.cpp` lowers `index`/`member`/`byte_extract` over
pointers to offsets from the root object. The natural hook is the **existing
`cprover_string_*` handling in `constant_propagate_assignment_with_side_
effects`** (`goto_symex.cpp:205`), which is already scoped to those
intrinsics (so C/C++/Java are untouched) but today only handles *constant*
content (`try_evaluate_constant_string`).

The clean variant: at a `cprover_string_*` call, for each string-pointer
argument, **re-materialise a bounded literal array** `array_exprt{[ *(p+0),
…, *(p+BOUND-1) ]}` by dereferencing each element through the value-set, and
rewrite the argument's content pointer to `address_of(index(that_array,
0))`. This routes through `find`'s existing `is_constant_array` fast path
(**no `array_pool` insertion → no loop crash**), needs **no front-end
storage change**, and works through variable indirection because the
value-set resolves `s.data` to its backing object.

*Advantages of symex-deref:* (1) avoids the pointer-identity crash entirely
(no map insertion); (2) handles loops naturally — each unwind dereferences
the current SSA content; (3) handles variable indirection (`s.data`) that
`find`'s syntactic fast path misses; (4) front-end keeps the current string
shape — no fresh-allocation refactor; (5) reuses the existing, JBMC-proven
refinement unchanged.

*Disadvantages / risks:* (1) **bounded unroll** — up to `PYTHON_MAX_STRING_
LENGTH` (64) element dereferences per string operand per call, each possibly
a guarded `if`-chain over value-set targets → formula-size / perf cost
(heavier than a single association); (2) it **touches core symex**
(`goto_symex.cpp`) shared with C/C++/Java — must stay strictly scoped to the
`cprover_string_*` path and be covered by the JBMC regression suite; (3)
**accumulation across iterations** (a string genuinely grown in a loop) is
*orthogonal* to deref-vs-association: if producers write into shared static
storage, the deref reads the current (aliased) content — correct for
within-iteration consumption, but a string accumulated across iterations
still needs per-execution storage for its *result*. In practice Python's
`concat` produces a fresh refinement result array, so the leaf-deref handles
the common cases; (4) the `BOUND` truncation is sound only because the model
already caps strings at 64 (consistent).

**Net recommendation.** symex-deref is feasible and is the **lower-front-end-
impact, loop-safe** route, reusing the existing refinement and `find` fast
path; its cost is bounded-deref performance and a carefully-scoped core-symex
change. The association route is equally sound but, to be loop-safe, requires
the per-execution-storage refactor (fresh allocation per producer). Suggested
order: prototype symex-deref on `chr(i)=="f"` + `github_3090_4` + the loop
case `github_3130_fail` *together* (the three that pin the design), measure
solver impact, and only then decide whether to generalise it across the
string producers or invest in per-execution storage. The SMT-string backend
(`--python-smt-strings`, CVC5/Z3) remains an orthogonal precision option.


### Prototype (2026-06-10) — symex-deref: concept validated, loop-safe, but naive integration regresses

Built a throwaway symex-deref prototype (reverted) to pin the design on
`chr(i)=="f"` + `github_3090_4` + `github_3130_fail` together. A
`language_mode=="python"`-gated hook in `symex_assign` intercepted
`cprover_string_equal/contains/is_prefix/is_suffix` applications and, for
each refined-string argument, **re-materialised a literal char array**
`[*(content+0) … *(content+n-1)]` via `value_set` dereference, rewriting the
content pointer to `address_of(index(<that array>, 0))` to route through
`array_pool`'s crash-free literal-array fast path.

**What worked (validates the approach):**
* `value_set` dereference resolves `*(s.data+k)` to the backing array
  element even through a member pointer (`s.data` → `__chr_arr_0[k]`) — *if*
  the content lives in a real symbol (the `chr` literal-temp had to be
  materialised into a symbol first; a dead literal temp doesn't resolve).
* Each deref'd element must be **L2-renamed** (`state.rename<L2>`) before
  going under `address_of`, otherwise `address_of` suppresses renaming and
  the array references the unconstrained base symbol.
* The literal array's **static size must equal the string length** (the
  fast path uses array size as length); the length is read from the symex
  **propagation map** (`try_evaluate_constant`), not `do_simplify`.
* With all three, `chr(i)=="f"` **proves soundly** (`chr(<nondet>)` and
  `chr(103)` still FAIL), `chr(122) not in "abc"` proves, and crucially
  **`github_3130_fail` does NOT crash** (loop-safe — the array_pool
  re-association crash is gone). This is the key advantage over the
  association route.

**What broke (why it's not shippable as-is):** the broad hook regressed **9
sweep tests** with **0 new gains**. The hook intercepts *all* python string
equality/contains, including cases that already worked and that
materialisation harms:
* **Constant strings / constant concats** (`github_3090_2`, `string13`):
  previously folded by symex constant-propagation or `find`'s native fast
  path; materialising at the comparison broke them.
* **Multibyte UTF-8 `chr`** (`casting-chr-var-multibyte`): `chr(cp≥0x80)`
  is a length>1 byte sequence; the prototype's leaf model / length handling
  didn't preserve it.
* **Concat results** (`github_3090_4/5`): the concat *result* string's
  content is produced by the refinement, not a symex symbol, so the deref
  can't materialise it — these stayed unproven.

**Conclusion.** symex-deref is **feasible and the right direction** — the
deref resolves real content, it is loop-safe, and it is sound. But a naive
"materialise every string comparison" hook is net-negative: it must be
**carefully scoped** to *only* the symbolic-content / variable-indirection
case it actually fixes, must **preserve multibyte and constant/literal fast
paths**, and must be **extended to concat results** (materialise producers,
not just comparison operands — i.e. bring the leaf into a symex-trackable
array at production time and let concat carry it). That is a real,
multi-part production change, not a point hook. Net for planning: keep
symex-deref as the favoured route, but budget it as a scoped feature
(producer-side materialisation + comparison-side use, gated to Python,
JBMC-safe), validated incrementally on the constant/multibyte/concat
cases — not a single `symex_assign` interception. Tree was reverted to the
PASS-2930 baseline.


## Design decision (2026-06-10): Choice B — symex content-pointer resolution

**Decision.** The production direction is **symex resolution of string
content pointers** (choice B), retiring the front-end `array_pool`-style
association in Python and, eventually, in Java. A string is just
`{length, char*}`; symex resolves the `char*` to its backing array object
via the **same value-set machinery used for every other pointer**, and the
refinement reasons over that array. The front-end no longer emits
`associate_array_to_pointer` / `register_string_with_solver` intrinsics.

**Why B over A (front-end-only association).** Both options share the same
prerequisite — Python string content must live in a real, symex-tracked
array object (a symbol array, SSA-versioned), not a dead literal temporary
or a fresh-unconstrained pool array. Given that, the only real difference is
*who connects the content pointer to its array*:
* **A (association):** the front-end emits explicit association intrinsics;
  loop-safety additionally requires distinct per-execution pointers (fresh
  allocation per produced string, JBMC-style).
* **B (symex resolution):** symex resolves the pointer like any other; no
  association intrinsics; loop-safe by SSA versioning (no pointer-identity
  map to collide).

B is **DRY** (one pointer-resolution path, not a second string-specific
one the front-end must feed correctly — the source of Python's string
bugs), **removes front-end burden and a footgun**, and is **loop-safe by
construction**. (An early version of this note also claimed B would let JBMC
drop its char-array association; the Java migration assessment below shows
that does **not** apply — Java's produced/heap-backed content genuinely needs
association, and the front-ends instead converged on association for produced
content.) A remains the safe, solver-untouching **fallback** if symex
integration proves too risky or regresses performance.

**Implementation discipline (non-negotiable):**
1. **Array-object resolution, not per-element unroll.** Resolve the content
   pointer to the *whole* array object (canonical
   `address_of(index(<object>, 0))` form) and let the refinement quantify
   over it symbolically, exactly as today. The reverted prototype's
   64×-per-element unroll was the source of its perf hit and its
   multibyte/constant regressions — *not* the approach itself.
2. **Preserve the existing fast paths.** Constant strings, string literals,
   and constant-folded concats must keep proving via symex
   constant-propagation and `find`'s native literal-array fast path. The
   new resolution must only engage where it adds value (symbolic content /
   variable indirection), never displace a working path.
3. **Preserve multibyte (UTF-8) content.** `chr(cp ≥ 0x80)` and any
   multi-byte content keep their true byte length; the array-object model
   carries the bytes as-is.
4. **Concat results are producers too.** Materialise content at the
   *producer* (leaf: literals/`chr`; results: concat), so a concat carries
   a real array object its consumers can resolve — not only at comparison
   operands.
5. **`array_pool` survives as the refinement-internal registry**, but it is
   *populated by symex resolution*, not front-end association. Extend
   `find`'s fast path to accept a **symbol**-array object
   (`address_of(index(<symbol>, 0))`), not only a literal `ID_array`.
6. **Gated to Python; JBMC untouched** in phase 1.

**Sequencing:**
1. **Python first** — gated to Python mode, array-object resolution,
   validated on the cases the prototype flagged (constant strings,
   multibyte `chr`, concat *results*) plus the three design-pinning tests
   (`chr(i)=="f"` SUCCESS, `github_3090_4` SUCCESS, `github_3130_fail`
   FAILED-no-crash) and soundness (`chr(<nondet>)` / `chr(103)` FAIL).
   Measure solver impact; no regressions vs the PASS-2930 baseline.
2. **Java migration — assessed, not applicable** (see the Java migration
   assessment below). Java's produced/heap-backed strings genuinely need the
   association mechanism; symex resolution applies only to static-value
   leaves, which Java lacks. JBMC is left untouched.

**Phase 1 landed (2026-06-10).** `goto_symext::resolve_python_string_content`
(gated `language_mode == "python"`) rewrites each refined-string argument of
any `cprover_string_*` application to a literal char array of its real
content, sized to the backing array's actual byte length and pinned to the
current SSA version, routing through `array_pool`'s existing crash-free
literal-array fast path (no association → loop-safe). A read-only value-set
pre-check fires the rewrite only when the content pointer resolves to a
single concrete array object, so refinement-produced (unconstrained) content
(e.g. concat results) and literal-array content are left untouched. The
front-end change is minimal: non-constant `chr()` stores its byte(s) in a
real symbol array (so the content survives variable indirection). The
`array_pool.find` extension explored earlier proved **unnecessary** — the
symex side emits literal `ID_array`s that the existing fast path already
resolves — and was dropped. Results: `chr(i)=="f"` (stored), concat chains
`github_3090_4/_5`, and `chr(...) not in s` prove soundly; `github_3130_fail`
is loop-safe; soundness holds; ESBMC sweep +`github_3090_4/_5` with **zero
regressions**; three Python suites green. Implementation discipline #1
(whole-content, not fixed-bound) is honoured by sizing to the real array
length — which also preserves multibyte UTF-8. Remaining for later phases:
broader content producers (slices, `str()` of ints, `join`, …) and the Java
migration.


### Phase 2 scoping (2026-06-10): the remaining gap is *chained ops on produced results*

Empirical probing after phase 1 shows phase 1 already covers more than the
leaf-comparison target. The following all prove soundly today:
* `chr(i)=="f"` stored in a variable; `chr(i) not in s`.
* Concat chains compared directly: `chr(i)+"z" == "fz"` (`github_3090_4/_5`).
* `startswith`, `len`, `in`/contains on a concat result.
* Subscript, iteration (`for c in s`), `startswith`, `s[0]==…` on a **chr
  leaf** stored in a variable.
* Subscript of a **single** `nondet_str()` object (`assume(s[0]=="a")` →
  `s[0]=="a"`).

The remaining gap is narrow and well-defined: **a byte-level / chained
operation applied to a refinement-*produced* result**, e.g. `(chr(i)+"z")[0]
== "f"`. Here `s[0]` lowers to `cprover_string_substring(s,0,1)` (Strategy
(b)) whose result then feeds `cprover_string_equal(res,"f")` — two chained
refinement ops where the middle operand `s` is itself a produced (concat)
result. Direct ops on a produced result connect fine; the *chain through a
produced result's content pointer* does not. The symex resolution of phase 1
cannot help here: a produced result's content is defined by the refinement,
not by symex (its `.data` pointer has no backing memory and the value-set is
`unknown`), so the value-set guard correctly leaves it untouched.

Root cause: a produced result is represented as `{nondet_len, nondet_ptr}`
(`make_nondet_string`) with no backing storage; the refinement connects two
uses of the *same* result pointer through `array_pool`'s per-pointer map, but
a chain that stores the result in a variable and reads it back, then feeds it
to a further intrinsic, does not reliably re-connect (SSA version / pointer
identity through the store-reload).

**This is exactly the "materialise producers / per-execution storage" item**
flagged in the design decision (discipline #4) and the original
storage-options analysis — the larger, higher-risk change. The tractable,
sound shape: give each `cprover_string_*` *result* a real symbol-array
backing (sized to its bounded max length) and have symex connect that array
to the result the same way phase 1 connects leaves, so downstream
byte-level/chained ops read real memory and `array_pool` never needs
association. This touches the shared refinement result path and must be
validated against the full sweep + suites for regressions before landing; it
is deferred to an explicitly-scoped phase 2 rather than bundled here, to keep
phase 1 a clean, low-risk landing.


### Phase 2 prototype measured (2026-06-10, reverted): producer-backing conflicts with the const-prop handlers

Prototyped the JBMC-style producer backing in `emit_string_function`: give
each produced result a real infinite backing char array, set
`content = &backing[0]`, and emit `cprover_associate_array_to_pointer` +
`cprover_associate_length_to_array`. Measured outcome:

* **It works for the target, outside loops.** `(chr(i)+"z")[0] == "f"` and
  other byte-level/chained ops on a *non-loop* produced result prove; phase 1
  cases, multibyte, and soundness all hold.
* **It crashes inside loops** (`for c in chr(i)+"z"`): the static backing
  re-associates the same content pointer every iteration →
  `array_pool`: "should not associate two arrays to the same pointer". JBMC
  avoids this only because it **allocates a fresh object per execution**;
  reproducing that needs per-iteration allocation, not a shared symbol.
  (Gating backing to non-loop call sites avoids the crash but leaves the loop
  case unfixed.)
* **Decisive blocker — it conflicts with the whole family of symex
  const-prop handlers.** `constant_propagate_{string_concat, string_substring,
  integer_to_string, delete, delete_char_at, set_length, set_char_at, trim,
  case_change, replace, ...}` each take the produced result's output operands
  and do `to_ssa_expr(f_l1.arguments().at(0/1))` — i.e. they require the
  output **length and content to be plain writable ssa symbols** that they
  write the computed result into. Changing `content` to
  `address_of(index(backing, 0))` fails `ssa_exprt::check` and **crashes
  `str(int)` / `"{}".format(int)`** (regression: `str-format-int-precision`,
  `limit-fstring`).

**Conclusion.** Producer-backing *by changing the result-content form in the
front-end* is **not a contained change**: the produced-result content is an
output lvalue the entire const-prop-handler family writes into as a plain ssa
symbol. Doing this properly means either (i) reworking those symex handlers
to allocate a real backing object for their output and write through it (the
JBMC pattern, lifted to the Python intrinsic path; also solves loop-safety
via per-execution allocation), or (ii) a separate post-production
materialisation pass. Both are substantial changes to the shared symex
string-result path and must be regression-gated against the full sweep +
JBMC. The prototype is reverted; phase 1 remains the clean landing, and the
`(produced-result)[i]` chained/byte-op case stays a documented residual until
this larger change is explicitly scheduled.


### Phase 2 landed (2026-06-10): produced-result backing in the symex handlers

The handler-rework succeeded where the front-end prototype could not. Rather
than changing the result-content *form* (which broke the const-prop handlers'
`to_ssa_expr` on the output), the backing is installed **inside symex, on the
symbolic path of the const-prop handlers**:

* `goto_symext::setup_python_string_result_backing` creates a **fresh
  per-execution** backing char array (`get_fresh_aux_symbol`, like the
  constant-string path's aux symbol — so loops never re-associate one
  pointer), points the result's content operand at `&backing[0]`, and emits
  `cprover_associate_array_to_pointer` + `cprover_associate_length_to_array`.
* It is invoked from `constant_propagate_assignment_with_side_effects` via a
  `with_python_backing(...)` wrapper around each producing handler: when the
  handler **constant-folds** (constant inputs) it already installs real
  backing and returns true unchanged; when it **fails** (symbolic inputs) and
  `language_mode == "python"`, the backing is installed and the handler's
  `false` is returned so the refinement still emits the operation's
  constraint over the now-backed array.
* The const-prop handlers themselves are **unchanged**, and the wrapper is a
  pure pass-through for non-Python modes, so **JBMC is provably untouched**
  (confirmed: `jbmc-strings` + `strings-smoke-tests` green).

Result: byte-level / chained operations on produced results now work and are
loop-safe — `(chr(i)+"oo")[0]=="f"`, `s[2]`, and `for c in (chr(i)+"z")`
prove; soundness holds (`(chr(<nondet>)+"z")[0]=="f"` FAILs); `str(int)` /
`format` constant cases are unaffected (constant path returns true → no
backing). ESBMC sweep: PASS 2932, **zero regressions**; three Python suites
green; new regression test `string-produced-subscript`. This closes the
`(produced-result)[i]` residual from phase-1's scoping.


### Java migration assessment (2026-06-10): not applicable — corrects the earlier "enables dropping Java association" framing

The original choice-B write-up speculated that symex content-pointer
resolution could eventually replace `java_string_library_preprocess`'s
explicit char-array association. Investigating the JBMC code, **that
unification does not apply** and the migration should **not** be done. The
reason is the *form* of Java's content array:

* **Java string literals** (`java_string_literals.cpp`): content is
  `address_of(index(<static symbol array>, 0))` — like Python literals.
  These could in principle use symex resolution, but it is one of three
  sites and on mature shipped code.
* **Java produced results, nondet inputs, and object-factory strings**
  (`make_nondet_string_expr`, `make_nondet_infinite_char_array`, the
  `code_assign_function_application` conversion path, `java_object_factory`):
  the content array is a **`dereference_exprt` of a freshly heap-allocated
  pointer** (`make_allocate_code(...); dereference_exprt{data_pointer}`).
  `array_pool::find`'s crash-free fast path resolves a literal `ID_array`
  and (with the phase-1/2 extension) an array-typed **symbol**, but **not a
  heap `dereference`**. So symex resolution cannot produce a fast-path-
  resolvable form for heap-backed Java content — **association is the
  natural and necessary mechanism** there.

**The unification that actually happened is the reverse.** Phase 2 gave
Python's *produced* results real backing via the very same
`cprover_associate_array_to_pointer` + `cprover_associate_length_to_array`
primitives JBMC uses (with fresh per-execution allocation for loop-safety).
So the two front-ends have **converged on association for produced /
heap-backed content**, and use symex resolution only for **static-value
leaves** (Python `chr`/literals). Java has no static-value leaf strings
beyond literals, so there is essentially **nothing to migrate**: dropping
JBMC's association would mean re-deriving the heap-array connection that
association already expresses, for a DRY-only benefit, at high regression
risk to a mature, shipped verifier.

**Decision: do not migrate JBMC.** Keep association as the shared mechanism
for produced/heap-backed content (Python and Java alike); keep symex
resolution for static-value leaves (Python). The choice-B design stands —
its scope is "leaves resolve via symex; produced/heap content associates" —
which both front-ends now follow. Java is left untouched.


### De-gating for shared code (2026-06-10): phase 2 de-gated; phase 1 stays gated

Goal: maximise shared, language-agnostic symex code and remove
`language_mode == "python"` checks from the engine, with Java leveraging the
shared mechanism. Outcome, by experiment:

**Phase 2 (produced-result backing): de-gated successfully.** The
`language_mode == "python"` gate was replaced with a semantic condition,
`string_result_already_backed()`, which checks (via the value-set) whether
the result's content operand already points to a concrete array object:
* JBMC assigns the result content `= &heap_char[0]` before symex → resolves
  to a concrete object → already backed → symex installs nothing (no double
  association).
* The Python front-end's bare `__string_ptr` resolves to nothing concrete →
  symex installs a fresh backing array + association.

So produced-result backing is now **shared, language-agnostic code**: any
front-end that produces an unbacked string result gets backing from symex,
and JBMC is handled by the *same* code path (correctly skipped) rather than
by a language check. `setup_string_result_backing` derives its symbol mode
from the content operand. Validated: `jbmc-strings` + `strings-smoke` green,
Python suites green, ESBMC sweep PASS 2932 / 0 regressions. (Committed.)

**Phase 1 (comparison-content resolution): cannot be cleanly de-gated.** Two
routes were tried and reverted:
1. *Remove the gate.* `resolve_python_string_content` materialises a literal
   char array from the content operand. That is only valid when the bytes are
   **symex-resolved** (Python's `chr` symbol arrays assigned a byte); for
   **refinement-constrained** content (JBMC's associated `char[]`) it produces
   a wrong/redundant array. De-gating **breaks `jbmc-strings`**
   (`java_concat`, `java_append_char`, `float-to-string`, ...). There is no
   clean symex-side condition distinguishing "symex-resolved" from
   "refinement-constrained" content (it depends on whether the front-end
   associated it, which is solver-side state symex cannot query).
2. *Eliminate phase 1 by routing `chr` through the shared backing* via
   `concat_char(empty, byte)` (a properly axiomatised intrinsic, unlike the
   `cprover_string_chr_func` stub). This made the result flow through phase-2
   backing, but the `empty_string` + `concat_char` + backing combination in a
   loop produced **"SAT checker inconsistent: UNSATISFIABLE"** (contradictory
   constraints — a soundness hazard) on `github_3130_fail`, plus a multibyte
   regression. Reverted.

**Net:** one of the two symex string hooks is now shared/language-agnostic
(phase 2). Phase 1 remains a single `language_mode == "python"`-gated hook;
removing it cleanly would require proper solver-side `chr` axioms (the
documented "Phase 3" infrastructure work) so Python leaves can flow through
the shared produced-result path without the `concat_char`/`empty_string`
inconsistency. Until that solver work is done, phase 1 stays gated — a small,
isolated, JBMC-inert specialization.


### Plan: solver-side `chr` axioms (to remove the last `language_mode` gate)

Goal: give `cprover_string_chr_func` real refined-string axioms so Python's
non-constant `chr(i)` can produce its result through the **shared** symex
produced-result backing (phase 2), making phase 1's Python-gated
content-resolution unnecessary — eliminating the last `language_mode` check.
A *dedicated* `chr` builtin (not the `concat_char(empty, …)` reroute) is the
key: it produces a length-1 result directly, with no `empty_string`
intermediate, which is what caused the SAT-inconsistency in the reverted
experiment.

**Solver (src/solvers/strings):**
1. Add `string_chr_builtin_functiont : public string_creation_builtin_functiont`
   in `string_builtin_function.{h,cpp}` (mirror `string_of_int_builtin_functiont`,
   which already creates a string from a value with no input string):
   - ctor: `PRECONDITION(fun_args.size() == 3)` (`result.length`,
     `&result[0]`, codepoint); `arg = fun_args[2]`.
   - `eval`: `make_string({ (char)arg }, length-1-array-type)`.
   - `constraints`: `result.length == 1`; `result[0] == typecast(arg, char)`;
     `return_code == 0`.
   - `length_constraint`: `result.length == 1`.
   - `name`: `"chr"`.
2. Register it in `string_dependenciest`'s `to_string_builtin_function`
   dispatch (`string_dependencies.cpp`) alongside `concat_char`/`of_int`.
3. Remove `ID_cprover_string_chr_func` from the no-axiom stub list in
   `string_constraint_generator_main.cpp` (it now has a real builtin).
   Note: this models the **bounded/truncated** char already used by the
   front-end for symbolic `chr` (one char wide); true multi-byte UTF-8 of a
   *symbolic* codepoint stays out of scope (constant `chr` keeps its exact
   `python_string_literal` multibyte path).

**Python front-end (src/python):**
4. Non-constant `chr` emits
   `emit_string_function(ID_cprover_string_chr_func, {codepoint}, …)` instead
   of building a symbol-array struct (revert component B). The result flows
   through the const-prop dispatch → `with_backing(false)` (already wired for
   producing funcs) → shared backing installs a fresh associated array; the
   `chr` axioms constrain `content[0]`. Loop-safe (fresh aux per execution),
   resolvable everywhere (comparisons, concat args) via the association.
5. Add `ID_cprover_string_chr_func` to the producing-func branch in
   `constant_propagate_assignment_with_side_effects` so `with_backing` runs.

**Cleanup once validated:**
6. Remove `resolve_python_string_content` and the last
   `if(language_mode == "python")` in `symex_assign` — symex string code is
   then fully language-agnostic.

**Validation gates:** the three design-pinning cases (`chr(i)=="f"` stored,
`github_3090_4/_5`, `github_3130_fail` FAILED-**no-SAT-inconsistency**),
soundness (`chr(<nondet>)`/`chr(103)` FAIL), multibyte (`casting-chr-var-
multibyte`), three Python suites, ESBMC sweep (no regressions vs PASS-2932),
and `jbmc-strings` + `strings-smoke` (the new builtin is shared — confirm
Java, which has no `chr` intrinsic today, is unaffected). Risk: a heavily
used builtin (`chr`) changes representation; the SAT-inconsistency that sank
the `concat_char` reroute must be re-checked specifically on the loop case.

### Java front-end: common-infrastructure assessment

**Finding: the genuinely common infrastructure is already shared; the Java
front-end's remaining code is irreducibly JVM-specific.** Both front-ends
emit the same `cprover_string_*` intrinsic vocabulary into the same shared
refined-string solver (`src/solvers/strings`, `array_pool`), and produced-
result backing/association (`cprover_associate_*`) is now installed by the
language-agnostic `setup_string_result_backing` in symex (Python) or the
front-end (Java) — both feeding the same solver contract.

What is left in `java_string_library_preprocess` (≈1900 lines) is JVM string-
object *mapping*: type predicates (`is_java_string_type`, …), `String`/
`char[]` heap-object layout, fresh heap allocation (`make_allocate_code`),
`code_assign_*_to_java_string`, and the Java-method→intrinsic conversion
table. This **cannot** move to common code: it encodes JVM semantics (the
`String.value` `char[]` must be a real heap object for array operations,
the object model, and GC), which symex's aux-symbol backing deliberately is
not. So Java cannot adopt `setup_string_result_backing` for its produced
results without losing `char[]` object semantics.

The associate helpers (`add_pointer_to_array_association` /
`add_array_to_length_association`) are *conceptually* the same as symex's
`associate_array_to_pointer`, but operate at different layers (front-end GOTO
emission vs symex SSA assignment), so factoring a shared helper would be
awkward and low-value.

**Where shared investment actually pays off:** not in moving Java front-end
code, but in the **solver** — implementing the still-stubbed
`cprover_string_*` axioms (`chr` above, plus `repeat`, `compare`, `strip`,
`split`, `index_of_from`). Those live in the shared `src/solvers/strings`
layer and benefit every front-end at once. That is the high-leverage
"more common infrastructure" direction for strings.


### chr-axiom implemented and tested (2026-06-10): fails to remove the gate — phase 1 stays

The chr-axiom plan above was implemented in full and reverted:
* Solver: added `string_chr_builtin_functiont` (a `string_creation_builtin_functiont`
  like `of_int`): `result.length==1`, `result[0]==(char)arg`, `return_code==0`,
  eval truncates the code point to the char width (mp_integer modulo, no
  bitwise ops). Registered in `string_dependencies`; removed `chr` from the
  no-axiom stub list.
* Front-end: non-constant `chr` emits `cprover_string_chr_func`; added it to
  the producing-func dispatch (`with_backing`).

**Result: the axiom itself is fine, but routing `chr` through the
produced-result backing regresses `github_3130_fail`** (`for i in range(...):
assert chr(i) not in s`) into a non-terminating **"SAT checker inconsistent:
UNSATISFIABLE"** refinement loop (~10^6 iterations). Crucially, this happens
**even with phase 1 fully disabled**, so it is not a phase-1 interaction: it
is fundamental to *association-based backing of a leaf used by `contains`
inside a loop*. The non-loop case (`chr(i) not in "xbm"`) is fine. A multibyte
regression (`casting-chr-var-multibyte`) also appeared.

**Root cause (the key lesson):** phase 1's **literal-materialisation** of leaf
content (`resolve_python_string_content` builds an `address_of(index(ID_array,
0))` that routes through `array_pool::find`'s **map-free** fast path) is
**loop-safe** — no per-pointer association entry, so no per-iteration
re-association and no inconsistent constraint accumulation. The phase-2
**association-based backing** (`associate_array_to_pointer`) is loop-safe for
*produced* results (concat/substring) but **not** for a leaf consumed by
`contains` in a loop. So materialisation and association are *not*
interchangeable; phase 1 is the correct mechanism for symex-resolved leaf
content, and the produced-result backing is correct for refinement-produced
content.

**Conclusion:** the last `language_mode == "python"` gate
(`resolve_python_string_content`) **stays**. It is not expedient — it encodes
a genuine distinction (symex-resolved leaf, loop-safe via materialisation)
that has no clean language-agnostic condition and that the association path
cannot replicate without the loop inconsistency. Four approaches to remove it
(de-gate; `concat_char` reroute; dedicated `chr` builtin; `chr` builtin with
phase 1 removed) all fail. Eliminating it would require making the refinement
itself loop-stable for associated leaves under `contains` — a solver-level
change beyond the scope of removing one gate.

The dedicated `chr` builtin (and the other stubbed axioms) remain worthwhile
as **solver precision** improvements, but `chr` specifically must not be wired
to the loop-fragile backing path for Python; it would only help a caller that
does not route the result through `contains`-in-a-loop.


### Query-intrinsic migration (2026-06-10): find/index landed; compare and producers deferred

Investigating "implement repeat/compare/strip/split/index_of_from" revealed
the Python front-end emits **none** of these refined-string intrinsics for
symbolic strings — symbolic string methods fall back to constrained-nondet or
byte-level approximations. So "implement the axioms" really means **migrate
front-end call-sites** to emit intrinsics whose axioms mostly already exist
(`index_of` already takes a from-index; `compare_to` already has axioms).
Split by risk:

* **`find` / `index` (queries) — LANDED.** Symbolic `str.find`/`str.index`
  now emit `cprover_string_index_of_func` (existing axiom; returns an int, no
  result string, so **no produced-result backing and loop-safe**). Precise
  and sound; ESBMC sweep **+`string-index-nondet`**, zero regressions. This
  is the realisation of "index_of_from" (index_of with an optional
  from-index). (Committed.)

* **`compare` (string ordering) — attempted via `compare_to`, reverted.**
  Routing symbolic `<`/`>`/`<=`/`>=` through `cprover_string_compare_to_func`
  did **not** improve precision: totality (`s<z or s>=z`), antisymmetry
  (`s<t ⟹ ¬t<s`), and even **constrained** 1-char `s<t` (with
  `assume s[0]=='a', t[0]=='b'`) all stayed unprovable. The compare_to result
  is not being tied to the (symbolic) content for the Python call shape —
  either the axioms under-determine the result or the content is
  insufficiently resolved for ordering. The pre-existing byte-level
  approximation (also imprecise for symbolic, but sound and exact for the
  common 1-char `is_digit`/`isalpha` pattern) is kept. Making symbolic
  ordering precise needs solver-side work (strengthen/connect the compare_to
  axioms to resolved content), not just a front-end reroute.

* **`repeat` / `strip` / `split` (producers) — deferred.** These produce
  *strings*, so they route through the produced-result backing and therefore
  share the `contains`-in-a-loop SAT-inconsistency that the chr experiment
  exposed. They should not be migrated until the refinement is made
  loop-stable for associated results under `contains` (the same solver-level
  fix that would unlock the chr gate).

**Net:** the safe, high-value query migration (`find`/`index`) landed (+1
sweep gain, 0 regressions); `compare` needs solver-axiom work; the producing
intrinsics are blocked on the same loop-stability fix as chr. The string
backend's precise, low-risk wins are now captured; further gains require
solver-internal loop-stability work in `src/solvers/strings`.


### Refinement loop-stability diagnosis (2026-06-11): membership over a symbolic-length needle does not converge

Dug into the "SAT checker inconsistent: UNSATISFIABLE" non-termination
(`string_refinementt::dec_solve`, `src/solvers/strings/string_refinement.cpp`).

**Clean reproduction on committed code** (no chr changes needed — phase-2
produced-result backing is enough):

```python
s = "xbmYZ"
for j in range(3):
    c: str = chr(nondet_int())   # fresh symbolic leaf per iteration
    t: str = c + "Q"             # produced (concat) result -> phase-2 backing
    assert t not in s            # membership (contains / not_contains)
```

* This **spins** (`dec_solve` runs >1000 refinement iterations, each "got SAT
  but the model is not correct", emitting ~10^5–10^6 SAT-inconsistent solves)
  until timeout.
* The **same without the loop** (`v2`) converges (FAILED).
* The **same with `==` instead of `not in`** (`v3`) converges.
* **Both `t in s` and `t not in s`** spin — it is the whole *membership*
  family, not just `not_contains`.

**Root cause.** Membership constraints (`cprover_string_contains` /
`string_not_contains_constraintt`) are refined by an index-set / witness
instantiation loop: each round the SAT model is checked, and if a membership
axiom is "violated" new indices are added and the axioms re-instantiated. When
the **needle** string has a **structurally symbolic length** — which a
produced/associated result has (its length is a nondet symbol constrained by
the producing op's axiom, e.g. `string_length#3`, *not* a literal `2`) — the
violated index sits at the symbolic length boundary (`violated_for:
univ_var=3` against a `string_length ≥ 6` bound), so `update_index_set` keeps
generating fresh indices derived from the symbolic length and **never reaches
a fixpoint**. A loop multiplies this (one fresh produced needle + membership
axiom per iteration), making non-convergence reliable. By contrast, phase-1
**literal-materialisation** gives the needle a literal char array with a
**structurally constant length**, so the index range is concrete and the
membership refinement converges — which is exactly why `chr`-via-phase-1
handles `github_3130_fail` but `chr`-via-backing (and produced results
generally) do not.

**Why it spins rather than erroring out:** `loop_bound_` is
`info.refinement_bound`, set to `DEFAULT_MAX_NB_REFINEMENT =
numeric_limits<size_t>::max()` (`solver_factory.cpp`). So the non-convergent
loop is effectively **unbounded** and runs to wall-clock timeout instead of
giving up.

**Fixability.**
* *Precise fix* (make membership refinement converge for symbolic-length
  needles): deep refinement-algorithm work in the shared `src/solvers/strings`
  index-set / witness instantiation — uncertain, and high regression risk to
  the mature JBMC string support. Not a contained change.
* *Sound robustness guard*: the unbounded `refinement_bound` means the
  pathological case hangs. A finite bound that, on expiry, returns the
  conservative `D_SATISFIABLE` (treat as counterexample — sound, already the
  behaviour for the "empty index set" path) would turn the hang into a
  bounded, sound "cannot prove" result. But `refinement_bound` is **shared
  with JBMC**, and lowering it risks cutting off legitimately-slow-but-
  convergent Java cases — so this must not be a blanket change.

**Recommendation.** This confirms phase-1 literal-materialisation is the
*correct* mechanism for leaves (concrete needle length → convergent
membership), and the last `language_mode == "python"` gate is justified.
The latent phase-2 case (produced result under membership *in a loop*) is a
real but currently-unexercised limitation; routing `chr` through backing is
therefore not worthwhile (it would trade phase-1's precise+convergent
behaviour for this non-convergence). Closing the gate and the
producing-intrinsic membership precision both depend on the precise solver
fix above, which is genuinely solver-research-scoped. If desired, a *sound*
defensive guard (finite bound → conservative `D_SATISFIABLE`, scoped so JBMC
is unaffected) would at least prevent the hang; it does not add precision.

### Evaluated upstream string-refinement commits (2026-06-11): none fix the loop-stability issue

Checked four sibling-branch commits against the membership-non-convergence
diagnosed above, by applying their `src/solvers/strings` hunks and testing the
repro (`for j in range(3): t = chr(nondet)+"Q"; assert t not in s`):

* **`5abee1595d`** (array_pool: allow re-association instead of the
  `INVARIANT("should not associate two arrays to the same pointer")`):
  applies cleanly, sound, and *does* remove the crash the original
  chr-association approach hit — but orthogonal to the spin (phase-2 already
  avoids that crash via fresh aux symbols).
* **`8fd1144b50` / `d86893623e`** (record `cprover_string_*` applications so
  refined-string axioms aren't lost under multi-assertion BMC): apply cleanly;
  a real precision fix for the *axioms-lost* scenario, but **does not** address
  the spin — with all four applied, the repro still does not terminate.
* **`c7fb844a60`** (on empty index set, add counter-examples for *all* violated
  axioms and **continue** the loop, instead of bailing): **conflicts with this
  branch's deliberate anti-hang design.** This branch already modified that
  exact path to *terminate* with a conservative `D_SATISFIABLE` on index-set
  exhaustion (sound over-approximation) precisely to avoid non-termination;
  c7fb inverts it to *continue*, which reintroduced a hang in the Python
  regression suite. So c7fb must **not** be cherry-picked as-is.

**Conclusion:** none of the four address the diagnosed root cause (membership
over a *symbolic-length* needle grows the index set without reaching a
fixpoint — the index set is never *empty*, so the exhaustion-path commits
don't engage, and the multi-assertion/array_pool commits target different
mechanisms). The precise fix remains the symbolic-length-needle convergence
work. `5abee1595d` / `8fd1144b50` / `d86893623e` are sound, orthogonal
improvements that could be cherry-picked for parity (with their own
validation); `c7fb844a60` is incompatible with this branch's exhaustion
handling.

### Update: the four commits are now cherry-picked (2026-06-11)

Per the goal of accumulating all available string-solver improvements, the
four commits were cherry-picked (solver hunks) and committed:
`5abee1595d` (array_pool re-association), `8fd1144b50` + `d86893623e`
(multi-assertion axiom recording), and `c7fb844a60` **adapted**. The
adaptation reconciles c7fb's "add counter-examples for all violated axioms and
continue" with this branch's anti-hang invariant: a bounded number
(`max_empty_index_set_rounds = 16`) of consecutive empty-index-set rounds,
after which we fall back to the conservative sound `D_SATISFIABLE` rather than
spin. This keeps the precision gain without the non-termination an unadapted
c7fb caused on symbolic-length-needle membership.

They still do **not** make symbolic-length-needle membership *converge*
(the repro `for j: t = produced(); assert t not in s` now *terminates* — soundly,
conservatively FAILED — instead of hanging, but is not proved precisely). The
precise convergence fix remains future solver-research work. Net validation:
ESBMC sweep PASS 2933, zero regressions; three Python suites + jbmc-strings +
strings-smoke green. `dict45_fail` (whose constant-string-key dict ops 8fd now
correctly refines, which had pushed it past the timeout) terminates in time
under the bounded guard with the correct verdict.
