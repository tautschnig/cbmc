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
