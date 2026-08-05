# Unbounded containers: SMT-array-backed lists and dicts

Status: proposal (2026-07-30). Companion to
[python-frontend-strings-plan.md](python-frontend-strings-plan.md) (the
native SMT-String backend this design deliberately mirrors) and to the
capacity-flag work that made `--python-max-list-length` real.

## 1. Problem, with measurements

Lists, dicts and sets are modelled as fixed-capacity structs: a
`{length, data[N]}` pair (lists), parallel key/value arrays (dicts),
`N = 16` by default. Three costs follow, all observed on real inputs:

1. **Bound failures.** Any program building a container past the
   capacity hits the `python-model-bound` property. On the AWS
   code-action corpus this is routine — most agent-generated
   comprehensions exceed 16 elements (reported independently by a
   user who recompiled to raise the limit before the flag worked).
   The failure is loud and fail-closed, which is correct — but it is
   a *model* bound, not a program property.
2. **Width blowup when raising N.** The whole struct is renamed on
   every assignment under symex field-sensitivity; capacity 64 means
   4× the slots of every list value flowing through `phi`/`merge`
   nodes, whether used or not. Raising the default trades bound
   failures for time everywhere else.
3. **Per-slot case splits.** Every subscript read/write lowers to an
   N-way indexed access; every dict lookup scans N key slots. With
   refined strings as keys this multiplies into the string solver:
   on `pyhard_exercise.py` (a 12-element fleet, two nested loops,
   string-keyed dict ops) the default backend spends 164 s in symex
   and then does not finish within 15 minutes; 45 % of cycles sit in
   the string-refinement equation rewrite (`replace_expr`), another
   15 % in its depth iterator, driven by the per-slot key equalities.

The strings story already solved the same problem once: the native
SMT-String backend (`--python-smt-strings`) replaced the
fixed-capacity byte-array model with the solver's unbounded `String`
sort, retired the refinement loop on that path, and introduced a
single representation invariant (in-aggregate storage via bv64
*handles*, value positions use the sort directly) with dispatch by
operand SORT in `smt2_conv`. This document proposes the same move for
lists and dicts.

## 2. Correctness constraints (PLR)

Any representation must preserve, not approximate, the following:

- **§3.2 sequences:** `len`, integer indexing with negative-index
  wraparound, `IndexError` on out-of-range, slicing, concatenation,
  repetition, element assignment, `in` via element equality.
- **§3.2 mappings + §3.7 language change:** dicts preserve
  **insertion order** (`list(d)`, `d.items()`, `popitem()` LIFO).
  A bare `(Array K V)` loses order — the model needs an explicit
  order component. (Same architectural shape as the JBMC
  LinkedHashMap order-overlay: a parallel ordered key sequence over
  an unordered store.)
- **`KeyError` on absent keys**, distinguishing "absent" from "maps
  to None" — the array needs a domain/presence representation, not a
  sentinel value.
- **§3.1 identity and mutation:** lists/dicts are mutable objects
  passed by reference; `xs.append` through a parameter must be
  visible to the caller. The existing by-reference parameter
  machinery (pointer-wrapped list/dict params) must keep working
  unchanged on top of the new value representation.
- **Equality is element-wise recursive** (`[1,[2]] == [1,[2]]`), and
  container elements are arbitrary values (the `python_value` tagged
  union), including strings — so the element sort must compose with
  the string backend's handle discipline.

## 3. Design

### 3.1 Representation

One new value shape per container kind, used **only under
`--python-smt-containers`** (SMT2 backend required, like
`--python-smt-strings`; the SAT backend keeps the bounded structs and
the capacity flags):

    list  ->  { length : Int,  data : (Array Int  Elem) }
    dict  ->  { size   : Int,  keys : (Array Int  Key),      // insertion order
                                vals : (Array Key  Elem),
                                dom  : (Array Key  Bool) }

- `Elem`/`Key` are the *slot* sorts: `python_value` stays the tagged
  union it is today (its `__str` member is already a handle; its
  container members become container handles, §3.3). Homogeneous
  int/float lists use the scalar sort directly — same precision
  ladder as today.
- The dict's `keys` array is the **insertion-order overlay**: index
  0..size-1 holds keys in insertion order; `vals`/`dom` provide the
  mapping. `d[k] = v` on a fresh key appends to `keys` and sets
  `dom[k]`; deletion compacts order lazily (a ghost `deleted` count,
  mirroring how the bounded model already handles it). Iteration,
  `list(d)` and `popitem` read `keys`; lookup reads `vals` guarded by
  `dom` (absent -> `KeyError` obligation).
- **Front-end vocabulary, not front-end encoding**: the converter
  emits generic `cprover_list_*` / `cprover_dict_*` function
  applications (get/set/append/len/contains/eq/slice/copy), exactly
  like `cprover_string_*`. `smt2_conv` lowers them by operand sort:
  Array-sorted operands -> `select`/`store`/`ite` terms; struct-sorted
  operands -> the existing bounded lowering. No mode flag inside the
  solver — the operand sort IS the dispatch (the smt2_conv discipline
  the strings retirement established).

### 3.2 Operation lowering sketch

| Python | SMT lowering |
|---|---|
| `len(xs)` | `length` field (Int — no 2^63 wrap; front-end asserts the practical bound like strings do) |
| `xs[i]` | bounds obligation `-length <= i < length`, then `(select data (ite (< i 0) (+ i length) i))` |
| `xs[i] = v` | same index normalisation, `(store ...)` |
| `xs.append(v)` | `store` at `length`, `length + 1` — **no capacity guard** |
| `xs == ys` | `length =` ∧ `(forall ((i Int)) (=> (and (<= 0 i) (< i length)) (= (select ...) (select ...))))` — quantified; see §5 |
| `v in xs` | existential — encoded as a fresh index witness + range constraint (mirrors `str.contains`'s solver-native form) |
| `d[k]` | `KeyError` obligation from `dom`, then `(select vals k)` |
| `d[k] = v` | `dom` store + conditional order-append |
| `for x in d` | index over `keys[0..size)` — order-correct by construction |
| slice/copy | fresh array + quantified frame, or (P1) bounded materialisation when the slice width folds |

### 3.3 Nesting: container handles

Containers inside `python_value` slots (and inside other containers)
reuse the **handle** discipline verbatim: a bv64 id whose denotation
is `(listtab h)` / `(dicttab h)` — uninterpreted functions to the
container sorts, declared solver-side exactly like `strtab`. The
write choke point stays `coerce_element`; the read choke points stay
the rvalue dispatches (`convert_attribute`, the Subscript branch).
This is deliberately the *same* whole-group shape that strings use,
because the failure mode it prevents is the same: representation
mixing at aggregate boundaries. (The `member_exprt` crash fixed
alongside this proposal was exactly such a mixing bug on the string
side — a refined-struct view built over an `ID_string` operand.)

### 3.4 What stays untouched

- The bounded structs and `--python-max-list-length` remain the SAT
  story and the default. This proposal adds a mode, it does not
  replace the model.
- The by-reference parameter machinery, `python_value` tagging,
  `value_equal`, iteration lowering and all property emission sit
  ABOVE the new vocabulary: they call the same generic ops.
- `--python-check-annotations` semantics are orthogonal.

## 4. Phasing (validation-gated, mirroring the strings plan)

- **P0 — vocabulary.** *(LANDED, 2026-08: `python_container_ops.cpp`.)*
  The per-slot key/element comparison is consolidated into
  `container_slot_equal` / `dict_slot_match` (plus
  `canonical_str_dict_type` / `boxed_dict_deref` for the boxed-dict
  layout), and all scan sites route through them. The consolidation
  immediately paid for itself: seven mutate-side copies (setdefault,
  pop, del, dict subscript write, list index/count/remove) had
  diverged to raw pointer key equality and falsely refuted any
  runtime-built needle (pinned by `container-runtime-needle`). The
  helpers take an explicit statement SINK so write-side scan loops
  sequence the string-solver equality snapshots with their local
  mutations. The C++-helper form (not yet function applications) was
  chosen so the SAT backend is untouched; P1 re-targets the helper
  BODIES to `cprover_list_*`/`cprover_dict_*` applications under the
  flag, call sites unchanged.
- **P1 — lists under the flag.** *(LANDED, 2026-08.)* Implemented
  with an INFINITE data array (`{length: i64, data: elem[inf]}`) —
  the machinery CBMC already has for C dynamic objects — rather than
  new solver vocabulary: symex and both backends handle it, so
  index/append/len/iteration/comprehension/slice/sort/eq flow through
  the existing lowering with the capacity guards suppressed
  (length-indexed stores are exact at any length). Construction is
  consolidated behind `build_list_data`/`build_list_value`;
  fold-layer literal reads go through the shape-agnostic
  `list_literal_element`/`list_literal_leading` decoders (a bounded
  array literal or the flag's store-chain). Operations that still
  scan a bounded prefix (sum/min/max, membership, structural ==,
  by-ref copy rebuilds, extend/concat/repetition producers) FAIL
  CLOSED via `emit_scan_bound_guard` / the retained count guards —
  python-model-bound at the use site, never silent truncation.
  Container retype-on-reassign VERSIONS the binding under the flag
  (the in-place symbol mutation emitted ill-typed GOTO that only
  boolbv leniency tolerated). Gate results: the 53-test list family
  runs under the flag with 51 verdict-identical (the 2 differences:
  the capacity-bound test — no capacity exists — and a both-verdicts
  desc); probes green on SAT, SMT2/z3 AND cvc5; >16-element
  comprehensions/appends verify with no bound properties. Pinned by
  smt-containers-core / -fail-closed / -indexerror. Backlog:
  length-driven copies for the producer family (extend/concat/
  repetition beyond the scan), quantified equality for
  beyond-the-scan `==`.
- **P2 — dicts.** *(LANDED, 2026-08.)* Implemented as the P1
  analogue, not the four-component overlay: the existing
  {length, keys[], values[]} layout with INFINITE arrays under the
  flag. The keys array IS the insertion order (PLR §3.7) — preserved
  by construction, including through mutation — so no separate
  overlay is needed; KeyError vs None-valued keys keep bounded-model
  precision. Every key lookup scans a bounded prefix and FAILS CLOSED
  past it, via a single guard in dict_slot_match (the P0 choke point:
  one edit covers get/setdefault/pop/del/subscript/membership/
  update). The scan cap is now runtime-configurable
  (--python-max-dict-size, previously slaved to
  --python-max-list-length) and — the point of the exercise — raising
  it no longer multiplies symex width: the dict value is 3 components
  regardless of cap instead of 1 + 2*cap renamed slots. Measured on
  pyhard_exercise (per-flag isolation, --unwind 16): SYMEX is
  211 s on the bounded default (SAT; 600 s+ DNF before the
  suffix-cache fix), 162 s bounded + --python-smt-strings --z3, and
  30 s with --python-smt-containers added — the containers flag
  contributes ~5x on top of native strings, confirming the
  field-sensitivity attribution. The SOLVER phase does not yet
  complete in ANY configuration (25 min budget): the plan's
  "solver time is not free" risk is now the active wall, and the
  benchmark study's next target. Dict fold
  readers were converted to a shape-agnostic dict_literal_leading
  decoder (get/membership/keys/values/items/update/int-key subscript);
  dict_literals cache surgery on subscript-assign is replaced by
  cache erasure under the flag (positional surgery corrupts store-
  chains); keys() adapts handle keys to handle slots
  (dict_key_for_slot — the pre-existing keys()-under-native-strings
  crash). Capacity guards: list appends suppress by an EXPLICIT
  parameter (never by cap-value comparison — the caps are
  independently configurable and may coincide); dict appends KEEP
  their guard as the write-side twin of the scan bound. Requires an
  SMT2 solver (warning wired, like --python-smt-strings). Gates: the
  60-test dict family runs 53/60 verdict-identical under the flag —
  the 7 differences are 2 solver timeouts and 5 loud failures
  (solver sort errors), ALL in dicts nested inside containers
  crossing call boundaries (P3 scope: nesting via handles); nothing
  fails silently. Pinned by smt-containers-dict / -dict-fail /
  -dict-cap. Full bounded suite verdict-identical.
- **P3a — nesting via the boxed-value handles.** *(LANDED, 2026-08.)*
  Not the listtab/dicttab UF design (mutation cannot live behind an
  immutable denotation function): under the flag, a CONTAINER-typed
  element/value slot is a boxed python_value — the existing
  __class_ptr/__list_ptr pointers ARE the handles, and CBMC-native
  pointers support in-place mutation, which PLR §3.1 requires for
  nested mutables (a dict read from a list is the object). One edit
  in each type constructor (python_list_type / python_dict_type)
  aligned the ANNOTATED nested types (List[Dict[...]]) with the
  literal displays, which already boxed; the P0–P2 boxed machinery
  (canonical layout, boxed get/subscript, decoders) covers the rest.
  `del`/mutation THROUGH an untyped parameter dispatches the boxed
  view by subscript type (a string key can only subscript a dict).
  Combined list+dict+nested differential: 109/115 verdict-identical.
  Residuals RESOLVED (2026-08-05): both crash classes shared ONE
  root — the unresolved-dereference byte-memory fallbacks
  (value_set_dereference) byte-imaged / scalar-typecast types with
  no byte layout; they now refuse non-byte-imageable types
  (non-trailing unbounded member — trailing FAM/VLA stays supported,
  union17) and fall through to the failure value, a fresh TYPED
  unconstrained symbol (sound). The false-alarm family's root was a
  replace-only boxed-write arm: d[k] = v through a box now INSERTS
  absent keys (PLR §6.4.6), and since a boxed COPY carries the same
  __class_ptr, iteration-variable mutation propagates (PLR §3.1).
  Remaining, all loud: cvc5's const-array-of-nondet strictness under
  native strings (z3 handles it); one knownbug retaining its
  bounded-only verdict. Combined differential 118/123.
- **The perf study**: pyhard_exercise and the corpus timeout tail as
  the benchmark set, capacity-64 bounded model as the baseline. The
  symex side is measured (211 s -> 30 s, see above); the SOLVER phase
  is the open wall.

## 5. Risks and open questions

- **Measured (2026-08-04): symex, not the solver, was the first wall.**
  Profiling `pyhard_exercise` showed 42 % of symex cycles in
  `get_fresh_aux_symbol` → `next_unused_suffix` — QUADRATIC aux-symbol
  minting (a from-zero linear probe per fresh name), driven by
  `value_set_dereference` failure-value minting on the front-end's
  boxed-value pointer derefs. Fixed upstream (PR #9083, cherry-picked
  here): `symbol_table_baset` now keeps the per-prefix resume hint that
  `symbol_table_buildert` pioneered (symex: 600 s+ DNF → 210 s
  complete). The post-fix profile puts field-sensitivity renaming at
  ~62 % — the direct target of this proposal's opaque-array dict values
  (P2): one array value instead of 16+16 renamed slots per dict. The
  full bounded-vs-array benchmark study remains gated on P2.
- **Quantifiers.** Element-wise `==` and iteration aggregates
  introduce ∀ over Int ranges. z3/cvc5 handle bounded-range
  quantifiers over arrays well, but combined with the String sort +
  UF handles this needs empirical validation early (P1 gate includes
  a solver matrix). Fallback: fuel-bounded unrolled equality up to a
  configurable rank, with the quantified form behind a sub-flag.
- **Solver time is not free.** The native-strings run of
  pyhard_exercise no longer crashes but z3 did not finish in 10
  minutes — moving work from symex into the solver is only a win if
  the encoding is small. P0..P2 must track solve time, not just
  verdicts.
- **Order-overlay cost for dicts.** Three arrays per dict value is
  heavier than one; programs that never iterate could use an
  order-erased two-component form. Deferred: measure first.
- **Sets.** The int-bitmap fast path stays; general sets become
  order-free dicts (`dom` only). Out of scope until P2 lands.
- **`is` identity for containers** stays address/handle-based —
  arrays are value sorts; identity must NOT become structural
  equality (PLR §3.1). The handle layer provides identity for free.

## 6. Relation to observed failure modes

| Observed | Bounded model | This proposal |
|---|---|---|
| model-bound at 16 (corpus-routine) | raise flag, pay width | gone (no capacity) |
| symex whole-struct renaming | grows with N | Int + Array values, constant width |
| dict-key string scans feeding the refinement loop | quadratic-ish | `select` on Key sort; no refinement loop under native strings |
| `member_exprt` mixing crash class | choke-point discipline (fixed) | same discipline, one more sort |
