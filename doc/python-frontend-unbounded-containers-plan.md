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

### 4.1 The solver-phase wall, characterized (2026-08-05)

  Grounded findings on `pyhard_exercise` (--unwind 16, containers +
  native strings unless noted):

  - **The formula**: ONE monolithic check-sat, 97 properties, 80 MB,
    44,918 asserts, ~50 K define-funs. Theory mix is NOT the
    suspect: zero fp terms in the hot cone, 586 stores, 3.5 K strtab
    UF applications, String equalities inlined (no str.* at all).
    --slice-formula removes nothing (everything is in the cone).
  - **Per-property decomposition localizes the wall**: 20 of 27 user
    assertions are solver-trivial (26 s end-to-end, of which ~25 s
    is symex). Seven (assertions 4/5/6/8/9/10/13 — all in the
    retry-loop cone) wall INDIVIDUALLY: the single-property
    assertion.4 slice is 57 MB (vs 26 KB for its neighbor
    assertion.3) and defeats z3 4.8.12, z3 4.13.4 and cvc5 1.2.1
    alike at 60 min direct-solve. z3 4.13.4's statistics at soft
    timeout show NO search stats after 900 s — it never exits
    preprocessing/bit-blasting (245 G allocations, 5.6 GB); cvc5
    grows to 15 GB. Not a search blowup: a formula-construction one.
  - **Root cause isolated by program surgery**: replacing ONLY the
    try/except in the retry loop with equivalent if/else control
    flow (same loops, same closure, same pow assertion, same calls,
    CPython-verified) drops assertion.4 from >3600 s to 28 s
    end-to-end. The wall is the exception-machinery encoding woven
    through a 4-deep loop nest (12-element builder x 3 pages x 11
    items x 4 retries): per-statement `__exception_active` guards
    compose with `__try_exc_before` snapshots (191 SSA versions,
    345 K occurrences in the a4 slice = 90 % of its definitions)
    into path-condition products that never fold, because instance
    reads through boxed pv dispatch stay symbolic at symex time even
    though the program is fully concrete. Unwind sensitivity
    confirms superlinearity: u=8 -> 864 SSA steps, u=12 -> 1,216,
    u=16 -> 155,751 (the fleet-builder loop needs 13, gating
    feasibility of everything downstream).
  - **Config isolation**: the wall reproduces in ALL four
    bounded/array x refined/native configurations — it predates and
    is independent of --python-smt-containers.
  - **Fix direction (whole-group), sharpened by three more
    surgeries**: the wall is specifically the try/ELSE snapshot
    encoding, and it is a SYMEX-phase phenomenon.
    (1) Keeping try/except but moving the else body into the try
    tail: 5 s (vs >3600 s). (2) Keeping try/else but replacing
    `break` with a loop flag: still walls — the break is innocent.
    (3) Capping the retry loop at its semantic depth (unwindset 4):
    still walls — not an iteration-count problem. The GOTO programs
    of the walled and fast variants are the SAME SIZE (612
    instructions at unwind 2); the 6x SSA expansion (155,751 vs
    26,251 steps for the identical property) happens during
    symbolic execution: the `__try_exc_before_N = __exception_active`
    snapshot plus the `if not __try_exc_before_N:` guard defeats the
    constant propagation that the direct per-statement
    `!__exception_active` weave enjoys, keeping both exception-state
    lineages (pre- and post-handler) alive per iteration. The fix
    seam is therefore symex-side constant propagation through the
    snapshot copy (or a frontend else-encoding that reuses the
    post-handler state instead of a snapshot where provably
    equivalent), NOT solver options, NOT unwind tuning.
  - **z3 vs cvc5**: on everything tractable the two are within noise
    of each other (26 s vs 26 s on pyhard tractable properties;
    0.2-1.1 s on the container family, cvc5 2x faster on the nested
    test). cvc5 needed two fixes to run at all: --arrays-exp on the
    command line (STORE_ALL) and a constant zero for the String sort
    in safe_zero (cvc5 rejects nondet inside `(as const ...)`); both
    landed. On the walled properties neither solver is better —
    same non-termination, so backend choice is NOT a lever here.
  - **Incremental backend blocked**: --incremental-smt2-solver
    routes python string intrinsics into the refined-strings solver
    (string_constraint_generator: unknown symbol __cbmc_strtab) —
    an integration gap; native strings currently require the legacy
    smt2_dec path.
  - Bonus finding: the monolithic run masked a FALSE ALARM —
    assertion.3 (`span.depth == 1` after `with span:`) fails; this
    is the documented context-manager arm of the instance-identity
    cluster (see instance-reference-semantics plan), surfaced only
    under per-property decomposition.

### 4.2 Outcome: the wall is FIXED (2026-08-05, same day)

  Four frontend fixes fell out of the characterization, each
  CPython-validated with minimal twins:

  1. **try/else re-encoding** (the wall itself): else body becomes
     the else ARM of the handler dispatch; no snapshot boolean. The
     hour-plus property: 5 s. Everything else: 26 s -> 5 s base.
  2. **Boxed-set membership** (a false-alarm family): the In tag
     chain gained its missing SET arm (bitmap through __class_ptr)
     and rvalue set arguments (ternaries) are materialized before
     SET-tagged boxing.
  3. **Per-instance heap boxing** (a FALSE-PROOF family): boxing a
     class instance used a per-syntactic-site STATIC temp, so
     loop-appended instances all aliased the LAST one
     (fleet[0].rid == last_rid VERIFIED, CPython raises).
     ID_allocate per boxing (mirroring closure capture records)
     gives each instance its own dynamic object.
  4. **Boxed-arg -> pointer-class param binding** (garbage reads):
     the struct->pointer bridge required raw ID_struct, so a boxed
     list element bound to `res: Resource` punned __int_val into
     the pointer; now __class_ptr is passed (PLR 3.1 mutation
     visibility included).

  End state on pyhard (97 properties, --unwind 16):
  - bounded+refined: full monolithic run 17 s END-TO-END (was: DNF
    at 25+ min), 16 loud findings — the four remaining counting
    false alarms trace to pyhard's OWN annotation lie
    (Resource.kind: str holding ints, reproduced minimally; the
    annotation-trust boundary, see the plans doc), plus documented
    unbound-local / model-bound / context-manager items.
  - containers+native: every property solves individually (46-74 s,
    ~45 s of it symex); only the 97-in-one-query run exceeds 30 min
    (linear accumulation, not the old single-property
    non-termination). Per-property decomposition is the practical
    configuration here pending the P2 perf work.
  - Combined container differential after the fixes: 128/132
    verdict-identical (two intended, two knownbugs) — the safe_zero
    string fix also cleared dict-native-string-key-box under cvc5.

### 4.3 Comprehensions: closed-form map encoding (2026-08-05)

  Comprehension loops were the re-entry point for boundedness under
  the flag (symbolic-length iterables truncate at the unwind bound).
  The map subset now lowers to `array_comprehension_exprt` — exact at
  any symbolic length, no unwinding. Analysis, backend matrix, subset
  taxonomy (map / aggregates / filter / dict), soundness gates and
  phasing: see
  [python-frontend-comprehension-closedform-plan.md](python-frontend-comprehension-closedform-plan.md).

### 4.4 Composition with --python-unbounded-ints (2026-08-06)

  Measured: the two flags COMPOSE exactly. Closed-form maps,
  quantified all()/membership/list-== and the witness index all
  verify over symbolic-length lists of MATHEMATICAL integers
  (10**27-scale element properties prove, twins fail correctly, no
  python-model-bound properties fire). Sort-wise the composition is
  (Array (_ BitVec 64) Int) -- bv indices, Int elements -- which z3
  handles including under the quantified lowerings.

  One intersection blocker, PRE-EXISTING to the unbounded-ints flag
  (not a containers composition gap): the `**` operator's
  variable-exponent lowering unifies its ite arms through FLOAT
  typecasts, which the smt2 backend cannot convert for mathematical
  ints (convert_typecast precondition; `2 ** n` crashes under the
  flag even standalone, and pyhard's backoff closure hits a
  simplifier type postcondition through the same chain). A proper
  fix wants a pv-unified or exact-Int pow lowering under the flag --
  z3's native (^ Int Int) was probed and reasons too weakly (unknown
  on 4-case bounded exponents), so the ±16-arm exact-int chain with
  wrapped float arms is the plausible design. Deferred as its own
  work item; pinned loud (crash, never a wrong verdict).

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


## Dict-family witness consolidation (2026-08-07)

The dict key-lookup family now has ONE encoding under
`--python-smt-containers` (this was previously six independent
bounded scans that could disagree with each other):

- `dict_key_matcher` (python_container_ops.cpp): THE per-slot key
  comparator (value-domain / string-content / handle-DENOTATION /
  IEEE-float). Handle probes compare by strtab image, never handle
  identity.
- `dict_lookup_witness[_members]`: the first-match-or-len witness --
  one forall assume, `found := w < len` a plain comparison. No
  exists, no iff (both break SAT-direction model queries).
- `emit_dict_store`: replace-or-insert choke point; the insert is a
  capacity-free length-indexed store into the infinite arrays.

Wired at: subscript read, `in`/`not in`, `get`, `setdefault`, `pop`
(with an exact lambda-array compaction preserving insertion order),
`d[k] = v` (plain / boxed / aug-assign), and dict literals of any
size (the over-capacity construction cut is bounded-mode-only now).
Stub dicts carry the key-UNIQUENESS well-formedness invariant, and
TypedDict stub returns model PEP 589 requiredness (optional keys
present in an arbitrary subset -- KeyError obligations on unguarded
reads).

**Solver ceilings (all encodings verified correct; these are
trigger-inference limits, the :pattern item):**
- Several same-key witnesses in one query (membership + get +
  subscript + get) can push Z3 past practical time; individual
  guards and all failure twins verify in seconds.
- The pop removal-completeness entailment ('k' not in d after pop:
  uniqueness + only-w-matched => no other occurrence) exceeds Z3's
  trigger inference but cvc5 proves it in 2s (pinned as a cvc5 CORE
  test smt-containers-dict-pop-removed-cvc5).
- 17+ membership conjuncts time out on both solvers.

Emitting `:pattern` annotations from the frontend (natural trigger:
`(select keys j)` / the strtab application) is the highest-value
next step for this family.


## Structure-of-arrays for List[TypedDict] (design, not started)

The one remaining perf-study class (d1/d4/repro.py -- the boto3
response shape `[a['appId'] for a in resp['apps']]`) is blocked on
REPRESENTATION, not encoding: List[TD] elements are pv BOXES (an
inline dict struct would nest infinite arrays -- no byte-lowering
width), per-element heap targets cannot be allocated for unbounded
length, and aliasing every element to one representative object
would prove FALSE equalities (ids[0] == ids[1]).

The study's own C validation of the representative+lift used flat
PER-FIELD arrays (`xs_data[j]` holding the field value directly) --
i.e. it presumes a structure-of-arrays representation:

  List[TD] with known fields f1..fn
    -> struct { length; f1_data[INF]; ...; fn_data[INF] }
  a = xs[j]; a['fi']   ->  fi_data[j]     (no pointer, no deref)

Everything downstream then composes with the machinery that already
landed: comprehension bodies over `a['fi']` become PURE terms
(closed-form maps -- no representative needed for the KeyError,
which vanishes for declared fields), quantified per-field shape
assumes replace the per-element heap synthesis, and NotRequired
presence becomes a per-field presence BITMAP array. Main work
items: the annotation seam (List[TD] -> SoA struct), stub
synthesis, subscript routing through the row view (`a` bound at
index j is not a first-class value; `a['fi']` must resolve to
fi_data[j] -- the binding needs conversion-time bookkeeping, the
same discipline as the comprehension's bound-variable substitution),
and aliasing rules (SoA lists are by-value CONTAINERS of scalars,
so PLR 3.1 reference semantics apply at the LIST level, not the
row level -- a row is a VIEW, and mutating xs[j]['fi'] writes
fi_data[j]).

Pinned: smt-containers-boxed-source-knownbug (flips when this
lands).


## SoA spike results (2026-08-07 evening -- LANDED as a gated spike)

The structure-of-arrays representation validated end-to-end: the
study's flagship class (repro.py, d1_bindname, d4_two_stage) all
VERIFY in <=1s, and ex1/ex3/ex5/ex6 drop from TIMEOUT to fast loud
verdicts. Design confirmations and the two findings that shaped it:

1. ROW = INDEX is the load-bearing idea and it composes: binding the
   comprehension variable as a signedbv[64] index makes a['f'] a
   pure select, the KeyError vanishes for declared fields, and the
   existing closed-form map applies verbatim. The OCCURS-CHECK
   escape gate is MANDATORY: an escaping row puns its index as the
   element value (demonstrated false proof, pinned in
   smt-containers-soa-escape-fail).
2. Value-set opacity of the infinite values array is the second
   wall behind the boxed representation (independent of shapes):
   two derefs of the SAME pointer expression read through it as
   unrelated failure objects, and two independent lookup witnesses
   of one key exceed solver quantifier budgets. ONE materialised
   copy per read region (td_field_read_memo / soa_value_of_name)
   resolves both; the copy must be invalidated at every possibly-
   mutating call (demonstrated stale-copy false proof, pinned in
   smt-containers-soa-stale-fail).

Spike SCOPE (the follow-up items for productisation):
- eligibility = all-required scalar-category fields; NotRequired
  needs per-field presence arrays; nested container fields need
  recursion or fallback;
- consumers wired: comprehension iterable (Name + inline
  subscript), len(); NOT yet: for-loops over SoA, xs[i] row reads
  outside comprehensions, membership, negative-index/slice ops;
- provenance is conversion-time (var_typeddict/var_soa_elem
  recorded at single-Name assigns; cleared on rebind) -- sound but
  loses precision through control-flow merges;
- the row-index pun is per-comprehension; a first-class ROW VIEW
  value (SoA pointer + index pair) would extend it to general
  bindings (r = xs[i]; r['f']).


## The __eq__/__hash__ soundness audit (2026-08-07 night)

Question settled: do the loop-free encodings bake in "eq is
structural equality"? The QUANTIFIED encodings were protected (their
gates bail to sound-nondet comparators for class tags), but the
shared CONVERSION-TIME folds were not -- and the bounded loops used
the same comparators, so this was never a loop-free-vs-loop
difference. Four false proofs demonstrated and killed (commit
'reject structural-equality gaps'): un-deduped definite lengths for
custom-__eq__ keys, lookup/membership constant folds matching
constructor TREES (identical trees are identical values for
builtins, distinct INSTANCES for classes -- Python default eq is
IDENTITY).

Doctrine going forward: python_eq_is_structural is THE predicate --
any new equality-consuming encoding (dedup, folds, scans, witnesses,
future sort/min/max keys) must consult it and reject or fall back to
tag-aware/nondet comparison when false. Follow-up (not started):
model user-__eq__ dispatch (a call per comparison -- viable in the
bounded scans, NOT under quantifiers; identity-eq for the default
case could be modeled precisely via object identity when
per-instance provenance is available). __hash__ is deliberately NOT
modeled: our containers are equality-keyed; the only observable
CPython divergence needs eq-equal objects with inconsistent hashes
(kept apart by CPython, merged by any equality-keyed model) -- that
shape is rejected by the same guard because such keys are class
instances.


## User-__eq__ dispatch tier + SoA for-loops (2026-08-07 late)

The audit's follow-up landed. Class-keyed dicts classify three ways
at build_dict_value: __eq__ without __hash__ -> TypeError
(unhashable, PLR 3.3); __eq__ + __hash__ -> the DISPATCH tier
(statement-level scans with a materialised __eq__ call per slot --
construction dedup is the replace-or-insert store semantics;
lookups honor user equality; the identity short-circuit is a sound
nondet when the probe could alias); default equality -> still
rejected (by-value keys lose identity; a first-class object
identity -- the ref-instances plan -- would lift this).
Quantified encodings remain closed to class keys by construction.

k5's residual is NOT an equality gap: methods whose receiver is not
literally named 'self' lose attribute binding (~20 sites key on the
name; pinned method-self-name-knownbug; whole-group fix = a
per-method receiver name recorded at def conversion).

SoA follow-up: check-only for-loops over SoA lists compose the
representative lift with the row-index binding. Remaining SoA
scope unchanged (NotRequired presence arrays, nested fields, xs[i]
row views, merge-safe provenance).


## Receiver-name fix, SoA productisation, identity-eq assessment
## (2026-08-08 batch)

- The method-self-name family is FIXED at its root: receiver typing
  is positional, body/AST scans use receiver_name_of_def, call-site
  method-ness consults method_receiver_param (instance methods only
  -- classmethods excluded). Free-function 'self'-named params are
  ordinary and now correctly pass by reference. Tuple-target
  attribute assigns register element-wise. k5's clash-semantics
  core verifies end-to-end.
- SoA: chained row-field reads xs[i]['f'] (IndexError obligation),
  loud bare-row rejection, and NotRequired per-row presence arrays
  (KeyError obligation on unguarded optional reads) landed.
  Remaining: persistent row-view bindings (r = xs[i]), exact
  optional-field comprehensions (presence check at the
  representative), nested container fields, merge-safe provenance.

**Identity-eq assessment (settled for now):** default-equality
class keys stay REJECTED. Identity of by-value instances is
unrepresentable -- every store copies, so `d[k]` with k naming the
inserted object cannot be linked to the stored copy. Two sound
routes exist: (a) the ref-instances plan (instances behind pointers
end-to-end) makes identity POINTER equality -- the right fix, big
prerequisite; (b) a narrow conversion-time tier (same-Name
same-binding tracking: d built with key Name k, looked up with k
unrebound) is implementable but carries exactly the stale-tracking
false-proof risk class this week's audits kept killing (rebinds,
aliasing, mutation through calls all invalidate it); if attempted
it must reuse the td_field_read_cache invalidation discipline
(clear at every possibly-mutating call). Not started.


## :pattern trigger spike (2026-08-10): NOT LANDED, verdict recorded

Tried both carriers: a frontend #trigger irep attribute (REJECTED --
an attribute-held term bypasses symex SSA renaming; the L0 symbols
crashed smt2_conv's identifier map) and solver-side derivation in
smt2_conv (the first select indexed by a bound variable, emitted as
`(! body :pattern (select))` -- syntactically clean, semantics-free).

Empirical verdict on the recorded ceilings: NET WASH. deep17
(17 stacked membership conjuncts) fell TIMEOUT -> SUCCESSFUL 20s,
and get_consist terminated (loud) -- but pop/setdefault mutation
sequences regressed SOLVED -> unknown: a :pattern RESTRICTS
instantiation to E-matching hits, and Z3's default MBQI-style
saturation was exactly what solved the mutation chains
(smt.mbqi=true does not recover it). Blanket emission trades one
ceiling for another; landing it requires SELECTIVE emission (e.g.
only on membership-shaped exists, or a per-quantifier frontend
choice carried INSIDE the formula, not as an attribute), plus the
cvc5 story re-checked per shape. The revert keeps today's
behaviour; the derivation patch is trivially reconstructible from
this record.


## Selective :pattern LANDED (2026-08-10); axiom-path fix; spikes B/C deferred

The exists-only selector resolves the blanket-emission wash:
EXISTS bodies (goal-directed membership) get the E-matching hint --
deep17 TIMEOUT -> ~20s, the four-witness get/membership consistency
query terminates; FORALL witnesses stay unannotated (saturation
solves the mutation chains). Landing this UNMASKED a real axiom
bug: once-per-intern strtab axioms were path-local to the first
interning branch (strtab(id) = "" on the other path -- encodings of
the same lookup disagreed); axioms now re-emit at every use.

NOTE: regression/cbmc carries FOUR failing python-* tests
(python-complex-pow, python-defaultdict-counter,
python-genexp-length-guard, python-power-fractional) -- all four
verified failing at cfc42d853ce~1, i.e. they PRE-DATE the entire
perf-study line and everything after it; not introduced here.
Worth a dedicated root-cause session (they are python tests living
in the cbmc suite, so the python-suite gate never covers them --
consider MOVING them into regression/python so the standard gate
catches regressions).

Spikes B (callee-side pointer returns: non-fresh factories
returning params/fields need the return-slot representation change
+ call-site coordination) and C (SoA row-view bindings r = xs[i]:
index binding + escape guard + rebind/merge discipline) are
DEFERRED to a fresh session -- both need unhurried multi-site
surgery; design notes stand in the respective plan sections.


## SoA row-view bindings LANDED (2026-08-10)

`r = xs[i]` binds r as a persistent ROW INDEX (the comprehension
shape made statement-durable): frozen normalized index at bind time
(negative indices per PLR 6.10.2), field reads through the existing
subscript hook, bare-use escapes loud-fail at convert_name, and the
memo invalidation choke points erase the BINDING while KEEPING the
name guarded (a dead view stays loud; rebinding clears both).
Remaining SoA items: nested container fields, exact optional-field
comprehensions (presence at the representative), merge-safe
provenance.

## regression/cbmc python-* root-cause batch (2026-08-10)

All four inherited failures resolved: two were CPython-FALSE test
expectations (27**(2/3) != 9.0; all(1 % x) over a list containing 1
-- cbmc's FAILED verdicts were correct); python-defaultdict-counter
was the 06-01 library-model defaultdict class shadowing the
intrinsic (removed; plain-assign consume seam + Pass 0.24 import
pre-scan added; residual str-value read-back and string-keyed
Counter accumulation pinned in collections-defaultdict-knownbug);
python-complex-pow was the PLR 6.16 binop LHS snapshot orphaning
constant tracking (entries now propagate to the temp). The core
cbmc suite is fully green again. Consider moving python-* tests
into regression/python so the standard gate covers them.


## Batch record (2026-08-11 pm): nested SoA fields, merge-safe
## provenance, cvc5 pattern verdict, test relocation

- Nested list fields: per-field MATRICES + per-row length arrays
  (len / element / row-view consumers exact, bare escapes loud).
- Merge-safe provenance: the SoA maps joined the EXISTING
  snapshot/merge protocol of convert_if -- the m7 wrong-owner
  false proof (row view bound to different owners per branch read
  the last-converted owner on BOTH paths) is dead; agreeing
  bindings survive exactly.
- :pattern on cvc5: the exists-only emission HOLDS on cvc5 --
  deep17 TIMEOUT -> 21s, pop_gone 2s, setdefault/get_consist fast;
  the lookup/consist_fail VERIFICATION ERRORs are a PRE-EXISTING
  cvc5 `STORE_ALL not supported` limitation (A/B with patterns
  stripped: identical), fixable via --arrays-exp in the solver
  invocation -- recorded as a follow-up, not a pattern issue.
- All 74 python-* tests moved out of regression/cbmc.

Remaining recorded: --arrays-exp for cvc5 invocations; the
k5 dict-iteration residual; ex4 solver-side provenance entailment.


## Batch record (2026-08-12): k5 features, ex4 SHIPPED, cvc5 verdict

- Dict iteration in comprehensions (keys-list view, PLR 6.10.1
  insertion order) and user-__eq__ SET displays (a set IS a dict
  with unit values -- build_dict_value_user_eq reused;
  first-occurrence dedup is the set rule). k5's remaining reds are
  CORRECT: its list({3,1,2}) asserts pin CPython's hash-order
  implementation detail, which PLR leaves unspecified -- k5 now
  encodes fully and became a solver-ceiling stress file (z3 >900s
  on the combined formula; every individual feature verifies fast).
- ex4 provenance entailment SHIPPED: filtered SoA comprehensions
  via a skolem witness array (forall-only; completeness
  deliberately under-constrained -- counting facts stay
  unprovable), plus the `if COND: return` early-exit scan via the
  first-match witness discipline. The pre-existing pointer_logic
  CRASH on non-check-only SoA loops is closed fail-closed. Study
  sweep: ex1/ex4/ex6 now VERIFY (were ceilings); ex5 fails on the
  INTENDED nondet-divisor ZeroDivisionError; ex2/ex7 remain the
  recorded bounded-fallback shapes; ex3 remains a solver unknown.
- cvc5 --arrays-exp: ALREADY in the invocation (smt2_dec.cpp);
  the residual lookup/consist_fail ERRORs are cvc5 'unknown' on
  SAT-direction model queries -- a recorded solver ceiling, not a
  missing flag.

PROCESS NOTE: a git-stash round-trip during a pre-existence check
silently held uncommitted spike work; the filtered-comprehension
arm was absent from two probe runs that "passed" vacuously through
the fallback. Re-verified after the pop: always re-run the acid
battery on the final tree.
