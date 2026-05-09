\file
# (a') Phase 2 — back-end abstraction design

Status: **design draft**, May 2026. This is the Phase 2
deliverable of the Python-string representation refactor
documented in ``python-string-representation-plan.md``. The
Phase 1 inventory (``python-string-phase1-inventory.md``) listed
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
