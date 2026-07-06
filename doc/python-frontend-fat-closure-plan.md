# Fat-closure: design & phased implementation plan

**Status: PLAN (no code yet).** This document captures the design for a
*fat-closure* representation that makes the remaining higher-order
closure cases sound and precise, and the phased, validation-gated plan
to build it. It is the forward-looking companion to
[§2 of the plans doc](python-frontend-plan.md#closures), which records
what has already landed.

---

## 1. What already works (do not regress)

Seven commits landed a closure cell substrate plus container dispatch,
all sweep-neutral (PASS 2945) and soundness-checked:

- **Named escaping closures** — read-only capture and `nonlocal`-mutating
  cells, with full multi-call PLR fidelity. Mechanisms: the *carry*
  (run the factory body, snapshot captures at the call site), per-
  invocation **heap cells** (`ID_allocate`) for mutated nonlocals, and
  **per-closure-variable binding** (`closure_var_captures`, keyed by the
  target name) for multi-call independence.
- **Higher-order dispatch through container subscripts** — `fns[i]()`,
  `d[k]()`, inline lists, constant or symbolic (guarded) selector, with
  argument coercion and capture appending.
- **Comprehension closure late-binding** — `[lambda: i for i in range(3)]`
  via a unique per-comprehension symbol bound to the final value (no
  enclosing-variable clobber).
- **Code-typed-capture guard** — a closure capturing another closure
  degrades to sound nondet instead of a symex abort.

These are **per-channel** mechanisms: each binds a closure's captures at
a recognised site using a key specific to that channel (the variable
name, the container element, the comprehension symbol).

## 1a. `del` of a captured variable — CLOSED without cell-capture (2026-07-06)

A separate soundness sub-case: `def f(): x=5; def g(): return x; del x; return
g()` raises NameError (the captured cell is unbound by `del`). This did NOT need
the fat-closure cell model. The read-only carry passes the enclosing variable as
a trailing call argument (`g()` -> `g(f::x)`), so guarding that capture-read at
the call site with the enclosing scope's per-name `<qname>$deleted` flag (the
existing del-name mechanism) closes it PRECISELY: a call before the `del` is fine,
after raises, and a rebind (`del x; x=9`) is defined again. Commit `be037ec01a`,
CORE `del-closure-nameerror`/`-nofp`. This is orthogonal to the value-capture
phases below (it concerns unbinding, not multi-call value identity).

## 2. Root cause of the remaining gaps (measured, not theorised)

The remaining gaps — **capture-through-param** (`apply(make())`),
**append/attribute-stored capturing closures**, and **compose**
(`twice(f)`) — all fail for one reason:

> An **anonymous** closure (no per-channel key) flows through a channel,
> and its captures are bound from a **shared or cached** location, which
> aliases across closure *instances*.

Two false proofs demonstrated this (both reverted):

1. **Per-site capture read.** The §12 monomorphised clone read inner's
   captures from the shared defining-scope symbol (`make::n`). With
   `apply(make(7))` then `apply(make(8))`, the single symbol aliases —
   we proved `b == 7` (CPython gives 8).
2. **Cached-clone staleness.** Threading captures via
   `closure_var_captures` keyed on the clone fails too: the clone is
   *cached*, so the binding is baked in at build time and the second
   call to a reused clone reads the first call's snapshot — the same
   false-proof shape.

The invariant we actually need: **each closure instance's captures must
be uniquely associated with that instance and travel with it.** The
per-channel keys satisfy this only where a key exists; anonymous
channels have none.

## 3. Architectural decision

Represent a closure as a **fat-closure value** that carries its captures
per-instance. Captures then travel through *any* channel (parameter,
container element, attribute, nested capture) by virtue of travelling
with the value, and a call binds captures from the value it actually
received — sound by construction, no per-channel key required.

This is the whole-group fix: the four landed per-channel mechanisms are
*special cases* of "associate captures with the instance"; the fat-
closure does it uniformly. (Phase 6 below can retire the per-channel
duplication; earlier phases keep it, additively, to bound risk.)

## 4. Representation

### 4.1 The closure value
Add a `CLOSURE` variant to the `python_value` tagged union
(`python_value_type.h`, `python_type_tagt`, next free tag = 10):

- `__int_val` holds an **fn identity** (a small integer index into a
  closure registry; see §6).
- `__class_ptr` (the existing opaque `pointer_typet{empty_typet}` slot,
  already shared by CLASS/DICT/COMPLEX/SET) points to a heap-allocated
  **capture record**.

Reusing `__class_ptr` (opaque pointer, cast at use site) avoids the
recursive-type / `smt2_conv` forward-reference problem that forced
`__list_ptr` to be opaque — a fat-closure capture record may itself
contain `python_value`s (a closure capturing a closure), so the pointer
must be opaque.

### 4.2 The capture record
A per-instance heap struct (`ID_allocate`, as for the cell substrate)
holding one field per captured free variable. Two field kinds:

- **By value** for read-only captures (snapshot at box time). Sound
  because the snapshot equals the late-binding value once the defining
  frame has returned (established by the read-only carry).
- **By cell pointer** for `nonlocal`-mutated captures — *reuse the
  existing heap-cell substrate*: the record field holds the cell
  pointer, so mutation through the closure persists and is shared. This
  is the key reuse that keeps `nonlocal` semantics correct.

Per-instance allocation (one record per box operation) is the soundness
crux: `apply(make(7))` and `apply(make(8))` box distinct records.

### 4.3 PLR notes
- Identity (`is`): two fat-closure values are identical iff their record
  pointers are equal. Boxing the *same* closure object twice (e.g. `g =
  make(); h = g`) must reuse the record (alias), not re-box, to preserve
  `g is h`. → boxing is keyed on the closure *binding*, not the syntactic
  site (see §6.3).
- A bare function (no free vars) need not be boxed; it can keep the
  existing code-symbol representation and the dispatch handles both.

## 5. Calling convention (the hard part)

Python closures have varying arity (`f()`, `f(x)`, `f(x, y)`, defaults,
`*args`, kwargs). A uniform dispatch must handle this. Decision:

- **Phase-gate by arity.** Support fixed positional arity first; pack the
  call's positional args into a small fixed-width `python_value` array
  (width `PYTHON_MAX_CLOSURE_ARGS`, e.g. 4), padding unused slots with a
  distinguished `MISSING` sentinel. The dispatch passes (record*, packed
  args); each closure reads its declared positional params from the
  packed slots and its captures from the record.
- **Unsupported shapes → sound nondet.** `*args`, `**kwargs`, keyword
  arguments at a fat-closure call, or arity beyond the max, fall back to
  the existing nondet path. **Never guess.** (This mirrors how the whole
  closure effort has treated undecidable shapes.)
- Keep the **lambda/def bodies unchanged** (they already take capture-
  params). The dispatch is a thin per-closure **thunk** that unpacks the
  record + packed args and calls the real body. Thunks share one
  signature → a single function-pointer type, so CBMC's function-pointer
  removal lowers the dispatch to a guarded choice over candidate thunks
  automatically. (Alternative: an explicit guarded dispatch over the
  registry filtered by arity — equivalent, no function pointers. Choose
  whichever validates cleaner in Phase 2.)

## 6. Boxing, registry, dispatch

### 6.1 Registry
A converter-side `std::vector` / map of boxed closures: index →
(thunk id, declared positional arity, capture field layout). The fn
identity stored in the value is this index.

### 6.2 Boxing sites (where a closure becomes a fat-closure value)
A closure is boxed when it flows into a **value** context:
- passed as a (non-monomorphised) argument to a function — *the param
  channel*;
- stored as a container element that is read back as a value;
- stored in an instance attribute;
- captured by another closure (the record field is itself a CLOSURE
  value — handles compose).

Box = allocate the capture record, snapshot/by-cell each capture, build
the `CLOSURE` `python_value`. Boxing happens **per evaluation** of the
closure expression → per-instance records → multi-call soundness.

### 6.3 Identity-preserving boxing
Box a given closure *binding* once and reuse the record on subsequent
flows of the same binding (so `g is g`), but box distinct *evaluations*
(distinct `make()` calls) separately. Practically: cache the box per
(closure-source-binding) within a scope; a fresh factory call is a fresh
binding.

### 6.4 Dispatch
In `convert_call`, when the callee evaluates to a `python_value` that may
be `CLOSURE` (a parameter, a container read, an attribute), emit: if tag
== CLOSURE → unpack (record*, fn index), pack the call's positional args,
dispatch to the thunk; else fall through to the existing paths. Guard an
unknown/empty registry to sound nondet.

## 7. Phased plan (each phase is a commit, validation-gated)

**Progress (landed):** Phase 1 (`d668f71438`), Phases 2+3
(`3b0077be2e`). The CLOSURE `python_value` variant, the registry,
`box_closure`/`dispatch_closure_value`, parameter-boundary boxing, and
the runtime dispatch are in. Capture-through-param works for read-only
and n-ary closures with **multi-call soundness** (`apply(make(7))` vs
`apply(make(8))` give 7 and 8; the unsound-if-shared `b==7` FAILS).
KEY SOUNDNESS LESSON: param captures are bound from the factory's call
**arguments**, because reading the param symbol after repeated calls to
the same factory is unreliable in symex (it returned the first call's
value → a false proof, now avoided). ORDERING LIMIT (sound nondet): the
HOF dispatch enumerates closures registered before its body is
converted, so a factory defined *after* the HOF degrades to nondet
until the function-pointer convention. REMAINING: Phase 4
(container/attribute boxing — append-built lists, instance attributes;
currently sound nondet), Phase 5 (compose / closure-capturing-closure;
currently sound nondet), Phase 6 (optional unification).

**Validation gate (every phase):** `regression/python` green; native
scan 0 crashes; ESBMC sweep 0 regressions vs PASS 2945; **plus** the
phase's soundness tests below. Revert the phase on any regression.

- **Phase 0 — tests first.** Add (initially failing/nondet) regression
  tests: `apply(make())` single + **multi-call distinct** (the `b == 7`
  false-proof guard *must FAIL*), `apply(make(), arg)`, append-built
  list call, instance-attribute closure, compose. Commit as
  `KNOWNBUG`-style or `--unwind`-gated expecting current behaviour, to be
  flipped per phase.
- **Phase 1 — representation (additive, no behaviour change).**
  `CLOSURE` tag, capture-record alloc helper, `box_closure` /
  `unbox_closure` / registry, thunk-emission scaffolding. Nothing calls
  it yet. Gate: builds, fully neutral.
- **Phase 2 — 0-arg dispatch + param boundary.** Box closures passed as
  ordinary (non-monomorphised) args; dispatch a `CLOSURE` callee with
  zero positional args. **Soundness gate: `apply(make(7))`/`make(8)`
  multi-call independent, and `b == 7` FAILS.** This is the phase that
  must not ship a false proof; do not proceed until it holds.
- **Phase 3 — n-ary arity convention.** Packed-arg dispatch for
  positional arity ≤ max; `apply(make(), 5)`, map-like use. Unsupported
  shapes → nondet (assert they degrade, not crash).
- **Phase 4 — container & attribute boundaries.** Box on
  `list.append`/element-store and on `self.attr = <closure>`; the
  existing subscript dispatch and attribute-call path recognise
  `CLOSURE` elements/fields. Append tracking must invalidate to nondet on
  non-straight-line mutation (no divergence from the runtime list).
- **Phase 5 — compose.** Capture record field holds a `CLOSURE` value
  (closure capturing a closure); `twice(f)` becomes precise. Replaces the
  code-typed-capture nondet guard for this case.
- **Phase 6 — unification (optional, only if it pays).** Migrate the
  named-variable / comprehension channels onto fat-closures and retire
  the per-channel duplication (`closure_var_captures`, the comprehension
  redirect), *iff* the duplication is causing bugs. Otherwise leave the
  landed mechanisms; the boundary-box is additive and they coexist.

## 8. Soundness invariants (PLR §4.2.2) — per-phase checklist

1. **Per-instance captures.** Distinct closure evaluations get distinct
   records. The canonical multi-call false-proof test must FAIL.
2. **Late binding via cells.** `nonlocal`-mutated captures share a cell;
   reuse the heap-cell substrate, do not snapshot by value.
3. **No guessing.** Any unsupported shape (arity, kwargs, unknown
   registry, symbolic closure identity) → nondet, never a concrete guess.
4. **No enclosing-scope clobber.** Boxing must not write enclosing
   same-named variables (the comprehension-scope lesson).
5. **Identity.** `is` consistent with record-pointer equality; same
   binding boxes once.
6. **Wrong-value assertions FAIL** and **unsound-if-shared assertions
   FAIL** in every phase's tests.

## 9. Risks & fallbacks

- **`python_value` / `smt2_conv` recursive types.** Mitigated by the
  opaque `__class_ptr` record pointer (same pattern as `__list_ptr`).
- **Arity convention complexity.** Bounded by the fixed-max + nondet
  fallback; generality is deferred, not faked.
- **Function-pointer-removal cost.** If the guarded lowering is large,
  switch to an explicit guarded dispatch over the per-call candidate set
  (the set is usually a singleton).
- **Migration regressions (Phase 6).** Optional and last; the additive
  boundary-box means we never *have* to migrate.
- **Genuinely undecidable residual.** A closure whose identity is not
  statically determinable (chosen by `random`/nondet, read from external
  input, or an untracked container) stays sound nondet — this is correct,
  not a gap, and is the "undecidable case" to confirm remains after the
  phases land.

## 10. Whole-group payoff

| Gap | Closed by |
|---|---|
| capture-through-param `apply(make())` | Phase 2–3 |
| append-built list of closures | Phase 4 |
| instance-attribute closure | Phase 4 |
| compose / closure-capturing-closure | Phase 5 |
| (named / container / comprehension) | already landed; optionally unified in Phase 6 |
| symbolic/unknown closure identity | remains sound nondet (undecidable) |

The fat-closure converts what looked like four independent point-fixes
into one mechanism, and subsumes the four already-landed per-channel
mechanisms as special cases.
