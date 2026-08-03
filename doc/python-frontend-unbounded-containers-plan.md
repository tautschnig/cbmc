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
- **P1 — lists under the flag.** Array-backed lists for scalar and
  pv elements; append/index/len/eq/in/slice-with-foldable-bounds.
  Gate: the pinning subset of `regression/python` (list semantics
  tests) passes under the flag with z3 AND cvc5; the AWS-corpus
  `python-model-bound` failures re-verify without bound properties.
- **P2 — dicts + order overlay.** The four-component dict; KeyError
  precision probes (absent vs None-valued); insertion-order pinning
  tests ported from the bounded model.
- **P3 — nesting via handles**, then the perf study: pyhard_exercise
  and the corpus timeout tail as the benchmark set, capacity-64
  bounded model as the baseline.

## 5. Risks and open questions

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
