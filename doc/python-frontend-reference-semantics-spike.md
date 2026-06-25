# Reference-semantics spike for anonymous mutable containers — scope

Status: **SCOPING (2026-06-24).** Not yet implemented. This document scopes a
focused, measurable spike to decide whether the frontend should model Python
mutable objects with **reference semantics** (heap-allocate + alias by pointer),
which would subsume — *precisely* — the whole group of nested-mutable-aliasing
issues that are today closed by sound but **imprecise guards**.

Audience: a contributor picking up the spike. Read alongside
[dict-value-byref-plan](python-frontend-dict-value-byref-plan.md) (the
per-instance-identity analysis) and
[dict-byref-plan](python-frontend-dict-byref-plan.md) (the value-string
perf analysis).

---

## 1. The whole group this would address

Python mutable objects (`list`, `dict`, `set`, class instances) have
**reference semantics**: binding, container storage, and parameter passing all
share *one* object; a mutation through any alias is visible through all. The
frontend stores *nested/anonymous* mutable elements **by value** (a copy in the
parent's `data[]` / `values[]` slot), so aliasing diverges. That single root
produces a whole group of issues, each currently handled by a *separate*
mechanism:

| symptom | today | precise fix |
|---|---|---|
| `r = c[i]; r.append(x)` (extraction-then-mutate) | **guarded** (havoc source on mutation — 2026-06-24) | reference semantics |
| `g = [[..]]*n; g[i].append()` (replication / self-append / new-container) | **guarded** (`python-model-bound`) | reference semantics |
| `a = {1: []}; a[1].append()` (dict-value-by-ref, literal) | int-keyed lvalue-slot (direct only); literal/extraction = guarded | reference semantics |
| dict / list / set passed to a function and mutated | by-ref for *named* args via `safe_typecast`; literals copy | reference semantics |
| object identity (`a is b`, `id()`) for anonymous objects | not modelled | reference semantics |

The guards are **sound** (over-approximate: the source becomes nondet, or a
model-bound property is reported) but **imprecise** — they lose all knowledge of
the mutated container. Reference semantics would replace every row above with
one **precise** model.

## 2. Why now — both historical blockers are resolved

The byref idea was previously shelved as "empirically untenable." That verdict
rested on two blockers; **both are now gone**:

1. **Perf ("the string explosion").** The cliff was *specifically* value-wrapped
   **strings**: a `dict[value,value]` held 16 `__str_ptr` pointers into 16
   refined-string objects, and the string-refinement solver exploded resolving
   them in bulk (see dict-byref-plan's diagnosis table: int-keyed
   `dict[value,value]` 0.42 s vs str-keyed TIMEOUT). The **opaque
   `__list_ptr`/`__class_ptr` byref was always cheap** (`nondet_dict2` ~2.1 s,
   `dict_fromkeys` 0.05 s — no cliff). The string half was then fixed outright:
   `__str` was inlined (`9da530b0e4`) and, on the native backend, **boxed**
   behind a fixed-width pointer this session. So the perf objection applied to a
   sub-problem that no longer exists; **container references never had it.**

2. **Per-instance identity.** The earlier spike promoted list/dict *literals* to
   heap symbols but used a **per-construction-SITE static counter**, so a literal
   built more than once (a loop, or a function returning `{k: []}` twice) shared
   one heap object and distinct containers aliased — a net regression, reverted.
   The dict-value-byref doc identified the fix ("Option 1: per-instance dynamic
   allocation … needs the allocation to participate in symex's per-unwinding
   renaming; a static symbol does not") but it was not available then.
   **It is now: P0's `allocate_boxed_leaf` (`side_effect_exprt(ID_allocate)`)
   gives a DISTINCT dynamic object per execution**, validated this session
   (function-returned and loop-built boxed leaves no longer alias —
   `leaf-box-no-alias-string`, `int-unbounded-box-sound`). That is exactly the
   per-instance anchor the literal/extraction cases were missing.

So the spike is no longer inventing a mechanism — it is **composing two
validated, in-tree building blocks** (the opaque-pointer byref representation +
per-instance dynamic allocation) and measuring the composition at corpus scale.

## 3. The model

Today (by value), a nested mutable element lives inline in the parent slot:

```
c = [[1],[2]]   →   python_list[ python_list[int] ]
                    c.data[0] = { length:1, data:[1,...] }   (a COPY)
r = c[0]            r := c.data[0]                            (another COPY)
r.append(99)        mutates r only                            (lost)
```

Under reference semantics, a mutable element is a **heap object** the parent
*points at* (the representation `make_python_value(LIST, &heap)` already used for
escaped names), with the heap object **allocated per construction**:

```
c = [[1],[2]]   →   for each inner list: h_i := allocate(); *h_i := [.. ]
                    c.data[i] = make_python_value(LIST, h_i)   (a POINTER)
r = c[0]            r := c.data[0]                             (copies the POINTER → alias)
r.append(99)        *h_0 .append(99)                          (propagates to c[0])
```

- **Extraction-then-mutate**: `r` copies the pointer → true alias → mutation
  propagates. Precise (no guard).
- **Multi-instance**: each construction calls `allocate()` → distinct object →
  no aliasing across instances. Precise (the old spike's regression is gone).
- **Reorder/reassign** (`insert`/`pop`/`sort`/`g[i]=…`): the slot moves the
  *pointer*, and `r` already holds the object's address → follows the object,
  not the slot (this is the exact CPython semantics the slot-aliasing approach
  got *wrong*; pointer-to-object is right).
- **Reads** (`c[i]`, `len`, membership) dereference the pointer — the machinery
  the escaped-name path already exercises.

## 4. The spike (minimal, measurable)

**Slice:** nested **list literals** only — `c = [[..], ..]` and
`c = [inner1, inner2]` where elements are list/dict literals. Smallest case that
exercises extraction, multi-instance, and reorder. Dicts/sets/instances are
out of scope for the spike (phase 4).

**Change (frontend-local, gated behind a temporary flag `--python-ref-mutables`
so it can be measured against the default):**
1. In the list constructor (`build_list_value` / `convert_list`), when an element
   is a mutable container, materialise it with `allocate_boxed_leaf`-style
   per-instance allocation and store `make_python_value(LIST/DICT, heap_ptr)` in
   the parent slot (element type becomes `python_value`). Reuse the existing
   escaped-name byref wrapping; only the *anchor* changes (dynamic alloc instead
   of a named symbol / static counter).
2. Subscript read already returns the slot value (the pointer) — verify it is
   *not* copied/unwrapped into a fresh struct on the read path.
3. The list mutators (`append`/… in `convert_expr_stmt` + `try_list_method`)
   already dispatch through the `python_value`/by-ref view; verify they mutate
   `*heap_ptr` in place.
4. With reference semantics live for the slice, the extraction-then-mutate guard
   and the replication/self-append `python-model-bound` guards become **dead** for
   it — leave them in (harmless) for the spike; their removal is phase 5.

**Measurements (the decision gate):**
- *Correctness/precision* — these flip from guarded-imprecise (or false-proof
  pre-guard) to **precise PASS**:
  - `g=[[1]]; r=g[0]; r.append(5); assert len(g[0])==2` (extraction) → SUCCESSFUL
  - `def mk(): return [[]]\na=mk(); b=mk(); a[0].append(5); assert len(b[0])==0`
    (multi-instance, the old spike's regression) → SUCCESSFUL
  - `g=[[1]]; r=g[0]; g[0]=[99]; r.append(5); assert g[0]==[99]` (reorder/reassign
    — slot-aliasing got this WRONG) → SUCCESSFUL
  - `g=[[1]]; r=g[0]; r.append(5); assert 5 in g[0]` → SUCCESSFUL (was the
    havoc-guarded over-approximation)
- *Soundness* — no new false proof: re-run the `haz`-style reassign/insert/pop/
  sort hazard suite; all must stay correct.
- *Perf* — the decision gate. Run the full `regression/python` suite + the
  ESBMC python sweep with `--python-ref-mutables` and compare wall-clock and
  PASS count to the default. **Pass bar:** ≤ ~10–15 % aggregate slowdown and **0
  new TIMEOUTs**; the per-benchmark watch list is the nested-container tests
  (`nested-container-*`, `nondet_list*`). The prior data says container pointers
  are cheap, so the expectation is *no cliff* — but `python_value`-element lists
  carry the per-element tagged-union + dynamic-object cost, which must be
  measured, not assumed.

**Success → proceed to phase rollout. Failure modes & response:**
- Perf cliff on some pattern → keep the flag opt-in; reference semantics becomes
  a precision *mode*, guards stay the default. Still a win.
- A soundness hazard surfaces (some reorder/alias path mismodelled) → fix or, if
  intractable in the slice, revert; the guards already keep the default sound.

## 5. Phasing (after a green spike)

1. **Spike** — nested list literals behind `--python-ref-mutables`; measure (§4).
2. **Lists** — make it the default for lists; **remove** the list
   extraction-then-mutate + replication/self-append/new-container guards (they
   become redundant). Re-validate soundness suite + sweep.
3. **Dicts** — dict *values* (literal + extraction); subsumes the int-keyed
   lvalue-slot special case and the dict-value-byref residuals. Then **sets**.
4. **Class instances** — heap-allocate instances; gives `is`/`id()` identity and
   removes the `__class_val_N` static-symbol aliasing (the same class as the leaf
   boxing). Largest blast radius; do last.
5. **Retire the guards** — delete `extracted_container_alias` / the
   `python-model-bound` nested-mutable guards / `is_aliased_list_element` once
   each container type is covered, and update the gaps inventory (the whole
   "Nested mutable-element aliasing" row collapses to "modelled precisely").

## 6. Risks

- **Perf at scale** (the headline unknown). Mitigation: the gating flag + the
  measurement gate; container pointers are cheap per prior data, but
  `python_value`-element overhead is real. Measure first.
- **`python_value` element-type churn.** Making nested mutable elements
  `python_value` touches element-type inference (`empty_list_inferred_types`),
  homogeneous-list fast paths, and read/coerce sites. Contained to lists in the
  spike.
- **Symex dynamic-object accounting.** Many allocations (one per nested element
  per construction) — confirm CBMC's dynamic-object handling scales (the leaf
  boxing already allocates per leaf without trouble at current sizes).
- **Interaction with the bounded model.** Heap objects are still bounded
  (`PYTHON_MAX_LIST_LENGTH`/`DICT_SIZE`); reference semantics does not change the
  capacity bound, only identity/aliasing.

## 7. Open questions

- Does `make_python_value(LIST, heap_ptr)` on the read path get **copied** by any
  intermediate (which would re-introduce a value copy and break aliasing)? Audit
  the subscript-read and assignment boundaries.
- Element-type inference for `c = [[]]`: the inner empty list still needs a value
  type; the existing empty-container prescan should carry over.
- Do nested references compose (`c=[[[]]]`) — a pointer to a list whose elements
  are pointers? Should fall out of the uniform representation; verify in the
  spike with one 3-deep case.
- Cost model: is per-element dynamic allocation cheaper or dearer than the
  current by-value copy + the guard's havoc? The measurement answers this.

## 8. Effort estimate

- Spike (list literals, flag-gated, measurement): **~1–2 focused days** — most
  pieces (byref wrapping, `allocate_boxed_leaf`, by-ref mutators) exist; the work
  is wiring literals to per-instance allocation + the measurement harness.
- Full rollout (phases 2–5) is a **multi-week** effort dominated by perf
  validation and the class-instance phase; gated on the spike's perf result.

---

## 9. Spike run log — 2026-06-24 (checkpoint: wrapping landed, extraction gap found)

Flag `--python-ref-mutables` is plumbed end-to-end and the **§4 step-1 wrapping**
is implemented: in `convert_list`, a nested list/dict/set-literal element is
materialised with per-instance `allocate_boxed_leaf` and stored as
`make_python_value(tag, heap_ptr)` (element type becomes `python_value`). Builds
clean; flag is **off by default** (zero impact on the default backend).

**Measured §4 gate (with the flag):**
- **Reads — PRECISE.** `g=[[1]]; assert len(g[0])==1` ✓; the single-index read
  returns the reference (`index_exprt{data,idx}` = the `python_value(LIST,ptr)`).
- **Direct-subscript mutation — PRECISE.** `g=[[1]]; g[0].append(5); assert
  len(g[0])==2` ✓ (the `obj.append` on a subscript writes back through the slot).
- **Multi-instance — PRECISE.** `mk()` twice no longer aliases ✓ (already worked
  via `allocate_boxed_leaf`).
- **Extraction-then-mutate — STILL FAILS** (the headline case): `g=[[1]];
  r=g[0]; r.append(5); assert len(g[0])==2` ✗. Root-caused precisely: the read
  returns the reference, but `r = g[0]` gives `r` its **own object** (a copy) and
  `r.append` grows that copy (`len(r)==2` ✓ but `g[0]` unchanged). The same holds
  for the named-escaped case (`inner=[1]; g=[inner]; r=g[0]; r.append`).
- **Soundness — OK so far.** The reassign/member-negative/multi-negative hazards
  give identical verdicts with and without the flag (no new false proof).
- **Perf — not yet measured** (premature while precision is blocked).

**KEY FINDING — §4 "verify steps 2–3 already work" is FALSE.** The minimal
wrapping is *necessary but not sufficient*. Achieving the headline
extraction-then-mutate precision requires the **assignment + mutator paths to
preserve and use the reference for a `python_value(LIST,ptr)`-typed VARIABLE**
(not just a subscript receiver):
  1. `r = c[i]` must alias (copy the pointer), not deep-copy the heap object —
     audit the assignment boundary (open question §7.1) for a value-copy of a
     `python_value`-wrapped element.
  2. `r.append(...)` where `r` is a `python_value(LIST,ptr)` variable must
     dereference `ptr` and mutate `*ptr` in place (today it grows `r`'s own
     object). The working `g[0].append` path mutates via the slot; the
     variable-receiver path needs the same deref-and-mutate-in-place.

**Status:** spike checkpoint committed (flag + wrapping, off by default). The
remaining work (extraction-aliasing + `python_value`-variable mutators) is the
substantive part of the ~1–2 day estimate and is the next step before the perf
gate is meaningful. No revert — the wrapping is sound and gated; it is the
foundation the extraction-aliasing builds on.

---

## 10. Spike run log — 2026-06-24 (extraction-aliasing: §4 precision GREEN)

Continuing from §9, the extraction-then-mutate gap is closed. Three bugs sat
between the wrapping and precise reference semantics; each was a *whole-group*
issue, not a point fix:

1. **The havoc guard defeated the reference (architectural root).** The
   extraction-then-mutate guard (`note_mutable_extraction` +
   `invalidate_extracted_source_on_mutation`) nondet-havocs the *source* on a
   mutation through an extracted alias — sound but imprecise, and it actively
   destroyed the precise result the reference now provides. Its own comment
   said "the precise fix is reference semantics." Fix: in
   `note_mutable_extraction`, under `ref_mutables`, **skip recording the alias
   when the extracted value is a `python_value` reference** (the wrapped case);
   a by-value (concrete list/dict/set-typed) subscript result still records the
   alias, so the unwrapped world stays sound. Single locus — the only place the
   alias is recorded. This made `r = g[0]; r.append` propagate (extraction +
   membership precise).

2. **Subscript-assign bit-reinterpreted a list into the slot.** `g[0] = [99]`
   stored a list-typed RHS into a `python_value` slot via `typecast_exprt`
   (byte reinterpret → corruption). Fix: under `ref_mutables`, wrap a mutable
   list RHS as a **fresh per-instance reference** (`make_python_value(LIST,
   allocate_boxed_leaf(...))`), so reassignment rebinds the slot to a NEW object
   and a prior extracted alias keeps the OLD one — exactly CPython `c[i] = …`.

3. **Read-back type mismatch (the real per-instance-identity subtlety).**
   `python_value_list` derefs `__list_ptr` as `list[python_value]`, but
   `allocate_boxed_leaf` boxed the inner list with its *natural* element type
   (e.g. `int`), so a nested value read (`g[0][0]`) reinterpreted the bytes →
   garbage (length read fine, hence `len` masked it). Fix: **canonicalise via
   `rebuild_list_as_pv` before boxing** — the same helper the escaped-name byref
   path already uses. This is the shared per-instance-identity representation;
   reusing it (rather than inventing a parallel one) is the whole-group move.

**Scope tightened to lists** (the spike's §4 slice); dicts/sets keep the
by-value guard (phase 3).

**Measured gate (`--python-ref-mutables`, OFF by default):**
- **§4 precision — ALL GREEN:** extraction (`r=g[0]; r.append; len(g[0])==2`),
  multi-instance (`mk()` twice, no cross-aliasing), reorder/reassign
  (`g[0]=[99]; r.append; g[0]==[99]`), membership (`5 in g[0]`), nested value
  reads (`g[0][0]==99`), and **3-deep composition** (`g=[[[1]]];
  g[0][0].append(5)`) all SUCCESSFUL.
- **Soundness — no false proof:** five genuinely-FALSE assertions
  (`len(g[0])==1` after append, `g[0]==[99,5]`, `99 in g[0]`, multi-instance
  negative, `len(r)==1`) all stay FAILED. `extraction-then-mutate-sound` stays
  FAILED — now for the *precise* reason (99 provably in `c[0]`) rather than the
  havoc over-approximation.
- **Default suite — green:** full `regression/python` all successful (23 skipped
  by design); the flag is off by default so the default path is untouched.
- **Perf — within bar:** nested/alias/extraction tests show unchanged verdicts
  and flat wall-clock with the flag; the heaviest (`nested-container-writes`)
  goes 25.9 s → 27.2 s (+5 %, within the ≤10–15 % gate), no new TIMEOUTs.

Two regression tests added: `ref-mutables-extraction` (precision) and
`ref-mutables-sound-neg` (genuinely-false stays FAILED).

**Decision:** spike PASSES its precision + soundness + (focused) perf gates for
lists. Next: phase 2 (make it the default for lists + retire the list
extraction/replication guards), then the full ESBMC sweep with the flag for the
at-scale perf number before flipping the default.

---

## 11. Phase 2 attempt — 2026-06-24 (at-scale gate NOT met; default stays opt-in)

Phase 2 (make list reference semantics the default + retire the subsumed
guards) was attempted and **gated by the full ESBMC sweep**. Result: **do not
flip the default yet.**

**What was done:** flipped the default ON (with a `--no-python-ref-mutables`
escape hatch), reran the full `regression/python` suite (green — only
`nested-list-alias-modelbound` needed updating, a precision *improvement*: its
`python-model-bound` cut is now a precise result, so it was rewritten +
renamed to `nested-list-alias-precise`), then ran the ESBMC sweep both ways via
a new `--extra-cbmc-flags` harness option for a per-test A/B diff.

**A/B sweep (unwind 10, timeout 60, jobs 14):**
- by-value (`--no-python-ref-mutables`): **PASS 2715**, DIFF 40, no crash.
- reference (default-on): **PASS 2710**, DIFF 44, **CRASH 1**.
- Per-test diff: **6 regressions, 1 improvement** (net −5). Regressions:
  `list31` (PASS→**CRASH**), `list-eq9` / `list_depth_test` (PASS→DIFF),
  `list_extend13` / `14` / `16` (PASS→DIFF). Improvement: `github_3667`
  (DIFF→PASS).

**Whole-group root (NOT six point fixes — one architectural gap).** The
by-reference representation (`list[python_value]`, pointer-aliased) is correctly
plumbed through *reads* and *mutators* (phase 1), but **not yet through the
value-semantic boundaries**, so wherever a wrapped list meets code that expects
the concrete nested type the seam breaks:
1. **`list == list` element comparison.** `python_value_structural_eq` already
   derefs nested LIST references recursively, but the **top-level** `list==list`
   path compares elements bitwise (pointer compare) instead of routing
   `python_value` elements through `python_value_structural_eq` → two distinct
   references for equal values compare unequal → spurious FAILED (`list-eq9`,
   `list_depth_test`). Sound (a spurious failure), but a precision regression.
2. **Concat (`+`) and `extend`.** Combining lists whose elements are references
   does not deref/normalise the elements (`list_extend13/14/16`).
3. **Call-argument / return coercion (the CRASH).** Passing a wrapped
   `list[python_value]` literal to a parameter annotated with a concrete nested
   type (`process_nested([[1,2]])` with `items: list[list[int]]`) assigns
   mismatched types in `value_set::assign` (`rhs.type() == lhs.type()` invariant)
   → abort. The reference representation must be coerced to/from the concrete
   nested type at the call/return boundary.

**Severity:** all six are sound (5 precision DIFFs + 1 crash; no false proof —
the crash aborts rather than mis-proves). But a corpus crash + 5 precision
regressions fail the **0-regression gate**, so per the §4 failure-mode policy
the default stays **opt-in** (`--python-ref-mutables` remains a precision mode;
the by-value guards remain the sound default).

**Phase-2 gate (acceptance set for a future default flip):** close the four
boundary sites above (route `python_value` elements through structural
deref/normalisation in list-equality / concat / extend, and add
`list[python_value]` ↔ concrete-nested coercion at the call/return boundary),
then the A/B sweep must show **0 regressions** (these six tests back to PASS,
no crash) before flipping. The guard-retirement step (phase-2 tail) is deferred
until then. Reverted the default flip; kept the validated opt-in work, the
renamed precise test, and the `--extra-cbmc-flags` sweep-harness option.
