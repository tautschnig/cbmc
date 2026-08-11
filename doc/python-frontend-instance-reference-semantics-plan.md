# Reference semantics for instances — design & phased plan

Status: **COMPLETE** (Phases 1+2+3 landed 2026-06-28, each validation-gated).
The whole-group fix for the **instance-identity cluster** of false proofs --
all five cluster knownbugs are now CORE; oracle baseline dropped 22 -> 17.

## 1. The whole-group root

Python objects have **reference identity** (PLR §3.1): binding, passing,
returning, or storing an instance does **not** copy it; all bindings refer to
the same object, and a mutation through one is visible through all. The CBMC
frontend represents an instance as a **by-value struct** in most positions, so a
mutation through one binding is invisible to the others — a **false proof**.

This single representation gap is the root of a cluster that otherwise looks like
five separate bugs:

| Manifestation | Pinning test (now CORE) | Copy site |
|---|---|---|
| local alias `b = a; b.x = …; a.x` | `instance-aliasing` | local assignment (struct copy) |
| composition `self.t = t; self.t.x = …` (read via the original) | `instance-field-aliasing`, `shared-object-aliasing` | **field store** |
| return-flow `u = f(v); u.x = …; v.x` | `instance-return-aliasing` | **return** |
| context-manager `with CM(v): …` mutating `v` via `self.t` | `context-manager-enter-mutation` | field store (composition) |
| concrete-class-param coercion PUN | (part of the coercion-boundary audit) | param→field copy |

**What already works** (and must keep working — `instance-ref-*` CORE tests):

- **Concrete-class parameters are already by-reference**: `def f(t: V)` types `t`
  as `pointer→python_class_V` (`python_converter_defs.cpp`, "Class instances are
  passed by reference"); attribute access on a pointer receiver dereferences
  (`python_converter_expressions.cpp` ~2364), so `t.x = …` writes the caller's
  object. (`instance-ref-param-mutation` is CORE.)
- **Any-typed slots preserve identity by address**: `coerce_to_typed_slot`
  wraps a class-instance Name bound to a `python_value` target as
  `make_python_value(CLASS, address_of(expr))` (`python_converter.cpp` ~3727), so
  an instance passed to an unannotated/`Any` parameter, or stored into a
  `python_value` field, keeps identity. (`instance-ref-any-param-mutation` CORE.)
- **`self` is a pointer**; **distinct instances stay distinct**
  (`instance-no-alias-distinct` CORE).

So the gap is precisely the **three remaining copy sites** where an instance is
held by *value*: **local assignment**, **field store**, and **return**. The fix
is to make those preserve the reference, *as parameters already do*.

## 2. Design

### 2.1 Representation choice

Two existing mechanisms preserve instance identity; the plan reuses them rather
than inventing a third:

- **(A) pointer-to-struct** — the representation parameters and `self` already
  use. `convert_name` auto-dereferences pointer-typed list/dict locals today
  (the alias_targets path); the same machinery extends to instance pointers.
- **(B) `python_value{CLASS, address_of}`** — the Any-boundary representation.
  Uniform with how instances already cross into `python_value` slots.

**Recommendation: (A) pointer-to-struct**, because (i) it matches the param/self
representation already in the tree, keeping the type system uniform; (ii)
attribute read/write deref is already implemented for pointer receivers; (iii)
it keeps the concrete class type visible (so `isinstance`, virtual dispatch, and
field typing keep working without unwrapping a tag). (B) is the fallback for
positions where a concrete pointer is awkward (e.g. a container element that is
already `python_value`-typed).

The unifying invariant to establish: **a binding/field/return slot that holds a
class instance is a pointer to the instance's storage, not a copy of it** — for
*all* of local, parameter, field, and return positions.

### 2.2 The three copy sites (what changes)

1. **Local assignment `b = a`** — extend the `alias_targets` pointer-promotion
   (which already aliases `b = a` for list/dict) to class instances, AND extend
   `convert_name`'s auto-deref condition (currently list/dict base only) to
   pointer-to-`python_class_*`, gated on `alias_targets` membership so `self`
   (a pointer that is *not* an alias) is untouched. (A 2026-06-26 spike confirmed
   the deref gap is the missing piece; see plan §0.)
2. **Field store `self.t = t`** — type an instance-valued field as a pointer to
   the instance (not a value struct), so storing the by-ref parameter keeps the
   reference. Requires: instance-field type inference to produce a pointer; the
   field-store path to assign the pointer (not deref-copy); attribute access
   `obj.f.attr` to deref the field pointer (one extra deref).
3. **Return `return t`** — return the instance by reference (pointer) so
   `u = f(v)` binds `u` to the same storage. Requires: the return slot/type to
   be a pointer; the call-result binding to keep it; `convert_name` deref of `u`.

### 2.3 Soundness (PLR §3.1)

- **Aliasing must be exact, never over-approximate.** The fix preserves identity
  for *genuine* aliases only; `instance-no-alias-distinct` (CORE) guards against
  over-aliasing unrelated instances. A spurious alias would be a *false proof*
  (a mutation wrongly believed visible) — unacceptable. Every phase must keep all
  `instance-*` CORE tests green and add 0 NEW oracle false proofs.
- **No new unsoundness from pointers**: the by-ref param path is already sound in
  the default config; extending the same representation does not add a soundness
  axis, only changes copy→alias at three sites.
- This is the **sound** direction: it *removes* false proofs (closes KNOWNBUGs)
  without introducing spurious failures, provided no over-aliasing.

## 3. Performance — the by-value-vs-by-reference BMC tension

The 2026-06 `--python-ref-mutables` spike (containers) established the real risk:
**precise nested aliasing under reference semantics needs per-element pointer
dereferences, and deep structural `==` blows up pointer analysis** (depth-2
nested-list `==` already TIMEOUTs; the A/B sweep regressed PASS 2710 vs by-value
2715, incl. a crash). That is why container reference semantics is **opt-in**
(`--python-ref-mutables`), not default.

For **instances** the picture is more favourable but must be measured:

- **Single-level instance pointers are already the default and fast** — params
  and `self` are pointers today, the suite is green, and an instance
  composition + field-equality probe ran in ~0 s. So instance-by-reference is
  *not* automatically the container cliff.
- **The risk is deep composition + structural `==`** (an object graph compared
  by value through several pointer levels). This is the per-phase perf gate.

**Decision (to confirm per phase):** instance reference semantics is a candidate
for **default-on** (unlike containers), because single-level instance pointers
are already default and tractable. If a phase's A/B sweep shows a regression
(TIMEOUT/crash) on deep-composition `==`, that phase falls back to **opt-in**
under the existing `--python-ref-mutables` umbrella (or a new
`--python-ref-instances`), keeping the by-value default for that position.

## 4. Phased plan (each phase: implement → validate-gate → flip its KNOWNBUG to CORE)

Ordering by **tractability × isolation** (least cross-cutting first):

- **Phase 1 — Return-flow** (`instance-return-aliasing-knownbug`).
- **Phase 1 — Return-flow** (`instance-return-aliasing`) — **DONE (2026-06-28).**
  A function returning a by-reference instance (`return t` for a concrete-class
  param / self / instance alias) returns the POINTER and promotes its return
  type; the caller's `u = f(v)` aliases the same object. Implemented as a
  whole-group extension of the EXISTING list/dict return-by-reference machinery
  (return-Name pointer path + caller pointer-RHS binding) via
  `is_instance_pointer(typet)`. **Key discovery:** instances are *cleaner* than
  containers — the call site already passes `address_of(v)` directly (no by-ref
  temp copy; the container path uses a `__byref_cont` temp, a separate
  list/dict-only gap NOT in scope here), so only the return-deref (`*t` → `t`)
  and caller-binding needed extending. **Soundness:** gated on a pointer-typed
  Name, so a fresh `return V()` stays by-value (distinct identity, no
  over-aliasing — validated). Flipped KNOWNBUG → CORE; suite green, sweep 0-reg,
  oracle 0 NEW.
- **Phase 2 — Local alias `b = a`** (`instance-aliasing`) — **DONE (2026-06-28).**
  Extended the `alias_targets` b=a alias block (convert_assign) to
  `python_class_*` instances + the `convert_name` auto-deref (gated on
  alias_targets membership so self/by-ref-params are untouched). The
  "upstream struct-copy" the spike saw was simply the alias block being gated on
  list/dict; extending the gate fired the promotion. **Soundness — alias
  invalidation:** a constructor rebind `b = V()` resets the symbol to a fresh
  struct + drops the alias (was writing through the stale pointer, corrupting
  `a`); a Name rebind `b = c` re-aliases; a non-instance rebind re-types
  normally. Both alias directions verified; distinct instances stay distinct.
  Flipped KNOWNBUG → CORE; suite green, sweep 0-reg, oracle resolved (baseline
  21→20).
- **Phase 3 — Field store / composition** (`instance-field-aliasing`,
  `shared-object-aliasing`, `context-manager-enter-mutation`) — **DONE
  (2026-06-28).** An instance-valued field assigned a by-REFERENCE value (a
  concrete-class param/alias) is typed pointer-to-instance, so it aliases.
  Reads / method dispatch / arg-passing already deref a pointer receiver; ALSO
  fixed the pre-existing isinstance-on-by-ref-instance gap (deref in isinstance,
  benefiting params too). **Soundness:** a FRESH construction (`self.t: C = C()`
  / constructor-RHS) keeps the OWNED struct type -> distinct per instance (only a
  by-reference RHS is pointer-typed); rebind re-aliases. **PERF: the
  deep-composition `==` cliff did NOT materialise** (3-deep composition +
  field-eq ~0 s; single-level instance pointers are what params already use). So
  this is **default-on, no opt-in needed** -- the key divergence from the
  container ref-semantics (which stays opt-in). 3 knownbugs -> CORE; suite green,
  sweep 0-reg, oracle 3 resolved.

**Per-phase validation gate (hard requirements):**
1. The phase's KNOWNBUG(s) flip to detection (FAILED) → promote to CORE.
2. ALL `instance-*` CORE guards stay green (no over-aliasing, no break of the
   already-working param/Any/self/no-alias behaviour).
3. Full `regression/python` suite green.
4. By-value corpus sweep: **0 regressions** (and record any GAINS); investigate
   any TIMEOUT/crash as the perf gate.
5. Oracle: **0 NEW** false proofs; the closed cases show as `resolved` →
   re-baseline + trim.
If a phase fails (2)–(4) and cannot be made sound+tractable as default, ship it
opt-in and document the residual.

## 5. Acceptance criteria

The cluster is "done" when these flip KNOWNBUG → CORE with all gates green:
`instance-aliasing` (✅ Phase 2), `instance-field-aliasing` (✅ Phase 3),
`instance-return-aliasing` (✅ Phase 1), `shared-object-aliasing` (✅ Phase 3),
`context-manager-enter-mutation` (✅ Phase 3). **ALL DONE.** The
concrete-class-param coercion PUN is closed as a corollary. The
concrete-class-param coercion PUN (coercion-boundary audit) is then also closed
as a corollary (the param→field copy disappears).

## 5b. Spike (2026-06-30): the remaining false-alarm cluster is instances-in-containers

A re-triage of the oracle's 206 false ALARMS (after the @dataclass construction
fix) found **150 in `laurel-encoding-soundness`, all assertion failures** (~73%
of the precision debt). Sampling them:

- *Already resolved* by Phases 1–3 + the @dataclass `__init__` binding:
  field-mutation-through-a-method, composition, augmented-assign field, multiple
  field-writes threading (`098`, `048`, `101`, `105` → 0 failures).
- *Still failing* — the dominant remaining sub-pattern: **an instance stored in
  a container** (`list[Task]`, dict of objects), mutated through an extracted
  element or a `for t in tasks: t.x = …` loop (`087`, `099`). Extracting a
  container element yields a by-VALUE copy, so the mutation is lost.

This is exactly the case Phases 1–3 deliberately did NOT cover: it is the
**nested-container `==` perf cliff** (`ref_mutables`). The A/B sweep in §3
regressed PASS 2710 vs 2719 when containers held by-reference values, because
nested-container structural equality blows up the solver. So this remaining
cluster is **perf-gated, not a free win**: making list/dict ELEMENTS
by-reference (so `tasks[i].x = …` and `for t in tasks: t.x = …` propagate) needs
the by-reference-container mechanism plus careful per-element-access perf
measurement — likely behind the `--python-ref-instances` flag with the by-value
default retained where the cliff bites. Recommendation: a dedicated, measured
"Phase 4 — container-element instances" effort, not a quick fix; the single-
level instance work is complete and the dataclass cluster is closed.

> **Generators ride along with Phase 4 (2026-07-01 spike #2).** A generator
> stored in a container (`box = [g()]; next(box[0])`) re-yields consumed elements
> for the same root: the generator (an eager `__gen_result` list) is copied
> by-VALUE into the slot, and its consumption cursor is a Name-keyed side-table
> that a subscript read cannot resolve. Making the generator a by-reference
> container element — carrying its cursor — is the same mechanism as this Phase 4
> and closes the `box[0]` channel for free. (The Name-resolvable generator
> channels — for / next / send / alias — are already closed via the cursor;
> `list(g)`/`sum(g)` on a Name are separate low-frequency per-builtin point-fixes,
> see plan §1.)

## 5c. Phase 4 plan — container-element instances (by-reference)

**Status: PLAN (spike-confirmed 2026-06-30).** This is the design for the
remaining precision cluster (~150 false ALARMS; `087_list_of_class_instances_
element_mutation`, `099_object_stored_in_container_stale_copy`). It is the only
remaining big precision whole-group; it is **perf-gated** (see §3, §5b).

**PLR §3.1.** A container holds *references* to objects. `xs[i].x = v`,
`for t in xs: t.x = v`, and a binding `t = xs[i]; t.x = v` all mutate the SAME
object that lives in the container; a later read through the container observes
the mutation. Today list/dict elements are by-VALUE structs, so extraction
copies and the mutation is lost — a sound-direction precision miss (cbmc FAILs a
valid program), confirmed: `for t in xs: t.x=1; assert xs[0].x==1` → FAILED in
~0.09 s (by-value is cheap but wrong).

**Representation.** Reuse the Phase 1–3 pointer-to-struct machinery: a list/dict
element that holds a class instance stores a **pointer to a per-instance heap
struct** (the same `make_python_value(CLASS, &heap)` / `allocate` shape already
used for single-level instances and for `--python-ref-mutables` list elements),
so the loop variable / extracted binding / subscript read all alias the stored
object. Gate behind **`--python-ref-instances`** (the single-level instance work
is default-on because its `==` cliff did not materialise; the container-element
case is exactly the nested-container `==` cliff, so it stays opt-in until a phase
is measured cliff-free).

**Access sites to change** (ordered cheapest-first by the §5b/§3 perf evidence —
nested-container *structural equality* is the cliff, plain field mutation is
not):
1. **`for t in xs: t.x = …`** — bind the loop variable to the element's
   by-reference slot (pointer), not a copy. The §5b spike found plain
   field-mutation-through-a-loop is cheap; this pattern dominates the witnesses
   ("iterate a list of objects and mutate each").
2. **`xs[i].x = …`** (subscript read-then-attribute-store) — return the
   element's lvalue slot so the attribute store writes through.
3. **`t = xs[i]; t.x = …`** (extract-then-mutate) — bind `t` by-reference (the
   extraction alias, mirroring the existing `extracted_container_alias` guard
   but aliasing instead of havocking).

**Per-phase perf gate (HARD).** Each access-site phase: run the by-value sweep
and require **0 PASS regressions and no new TIMEOUT**; if a phase reintroduces
the nested-container `==` blow-up (the A/B sweep's 2710-vs-2719 regression),
**keep by-value for that site** and document it. Measure with
`--python-ref-instances` ON vs OFF on the corpus; the flag default stays OFF
until/unless a site is proven cliff-free.

**Soundness.** No over-aliasing: a FRESH element (`xs.append(T())`) stays
owned/by-value-identity like a fresh single-level instance; only an element
EXTRACTED-and-mutated or iterated aliases the stored object. Dicts/sets stay
guarded (same representation limit as [dict-value-byref](python-frontend-dict-value-byref-plan.md)).

**Acceptance criteria.** Gates (under `--python-ref-instances`): `087`/`099`
witnesses → SUCCESSFUL (the false alarm clears) AND a *genuinely* false post-
mutation assert still FAILs. Regression: the single-level instance tests
(`instance-return-aliasing`, `instance-aliasing`, `instance-field-aliasing`,
`shared-object-aliasing`, `context-manager-enter-mutation`) stay green; default
(flag-off) suite + sweep unchanged (0-reg); oracle 0-NEW. Pin the witnesses as
`container-element-instance-knownbug` until Phase 4 lands, then flip to CORE
(flag-gated).

### Phase-4 perf re-spike (2026-07-02) — ASSESS ONLY; verdict: DEFER (flag-gated)

Re-measured and re-scoped the last remaining container-element residuals
(`gen_container` soundness + the ~150 instance-container-mutation precision
alarms). Findings:
- **The cliff is real and there is no free lunch.** By-VALUE containers compare
  `==` cheaply at every depth (measured: flat / depth-1 / depth-2 all ~0.1 s).
  The SOUND by-reference representation stores an element as a heap pointer
  (`make_python_value(LIST, &heap)`), which makes nested `==` compare pointers;
  the in-tree `--python-ref-mutables` avoids a deep-compare TIMEOUT by returning
  **nondet** for distinct references (so `[[1]] == [[1]]` is unprovable) — sound
  but a PRECISION loss, which is exactly the A/B-sweep 2719→2710 regression.
- **The cheaper alternative (slot-aliasing: bind the access var to
  `&container.data[i]`) is UNSOUND** for reassign/reorder (`r = g[0]; g[0] = X;
  <mutate r>` must follow the OBJECT, not the slot — documented in the spike doc
  §4). So the sound design must be element-as-heap-pointer, which carries the
  nested-`==` precision cost. No cliff-free sound shortcut exists.
- **Neither residual is covered by the existing `--python-ref-mutables`** (which
  handles only nested list/dict elements): `gen_container` and
  instance-in-container both still FAIL under it — they need the unimplemented
  `--python-ref-instances` Phase 4. `gen_container` does NO `==`, so it would not
  itself hit the precision cost, but keying a generator's cursor on a container
  slot needs the same by-reference-object machinery (a bespoke generator-only
  heap identity would duplicate Phase 4 for one niche case — rejected).

*Verdict: DEFER, flag-gated.* Phase 4 is worth doing as an **opt-in
`--python-ref-instances`** (default OFF), phased cheapest-site-first (for-loop
mutation → subscript store → extract-then-mutate), each site behind the HARD
per-phase precision+perf gate (0 default-config regressions; nested-`==` stays
by-value/precise unless the user opts in). The default stays by-value (cheap +
sound via the extraction/replication guards). `gen_container` rides Phase 4 and
remains the single pinned KNOWNBUG until then. Not shipped this pass: it is a
dedicated multi-day flag-gated effort whose payoff is OPT-IN precision (~150
alarms) plus one niche soundness residual, not a spike-sized change.

### `del c.a` (del-attr) rides the same per-instance-identity family (2026-07-03 spike)

The `del c.a; read c.a → AttributeError` residual (now CLOSED — `del-attr-read`,
CORE; see **IMPLEMENTED** below) is the SAME per-instance-identity family. Spike
outcome (the design that was implemented):
- **Sound design:** a per-instance **deleted-flag** carried WITH the object — a
  `__shadow_<attr>`-style bool struct field (the machinery to add such fields
  and to emit `python-attribute-error` on an unshadowed read already exists for
  class-level / bare-annotation attrs — defs.cpp `__shadow_` + `class_attrerror_
  fields` + the convert_attribute read-guard). `del c.a` sets it deleted, a
  `self.a =`/`c.a =` store clears it, a read emits a conditional AttributeError.
  **Sound under aliasing for free:** `d = c` is already a pointer to `c`'s struct
  (Phase-2 instance-by-reference, default-on), so `del c.a; d = c; d.a` reads
  `c`'s deleted flag and raises — this is exactly why the flag must live on the
  OBJECT (a scope-local per-name flag, like del-name, would be unsound here).
- **Why deferred:** it is a struct-LAYOUT change for every del'd instance
  attribute (constructor init to bound, a clear at every `self.a =`, the read
  guard) — moderately invasive, rippling to constructors / struct equality /
  leaf-boxing — for a niche pattern. It belongs with the Phase-4 per-instance
  work: `del_attr` + `gen_container` are the two remaining fuzzer residuals and
  BOTH are per-instance object identity, so a single per-instance-state effort
  (the flag-gated `--python-ref-instances` object-identity infrastructure) closes
  both. Kept pinned until then; the design above is implementation-ready.

### IMPLEMENTED (2026-07-06, `41d6788612`)

The de-risked plan below was implemented and CLOSES del_attr. A per-instance
`__present_<attr>` bool field is added ONLY for INSTANCE-ONLY del'd attrs (class-
level attrs keep the __shadow_ class-fallback path -- over-applying the guard to
them caused a class10 regression, caught by the sweep and fixed by the class-level
skip). Store->true (the 5 maybe_shadow_assign sites), del->false (Delete handler,
before value handling), read->AttributeError when false (both convert_attribute
paths). Sound under aliasing via by-reference instances; precise (read-before-del /
rebind / distinct-instance / class-fallback all correct). CORE del-attr-read/-nofp;
fuzzer del_attr_read resolved. Only gen_container remains in this root (genuine
by-value Phase-4).

### Re-spike (2026-07-06) — NOT blocked on Phase-4; concrete de-risked plan

Re-examined `del_attr` with the "is there a cheap del_in_closure-style path?"
lens (that lens closed del_in_closure via the existing deleted-flag). Findings:
- **No cheap reuse.** The `getattr_deletable_fields` scan (defs.cpp) is gated on
  the class defining `__getattr__`; the `__shadow_<attr>` machinery
  (`maybe_shadow_assign`, `class_level_attrs`, the convert_attribute read-guard)
  covers only attrs WITH a class-level default, and its shadow-false fallback
  reads *class storage* — but a del'd instance-only attr has none, so shadow-false
  must instead raise AttributeError. So there is no ready flag to thread (contrast
  del_in_closure, which just needed the existing name flag at the capture-read).
- **Correction: NOT blocked on Phase-4.** Instances are already by-reference
  (Phase-2 default-on), so a per-instance flag *field* is shared through aliases
  (`b = c; del c.a; b.a`) for free. So `del_attr` can land INDEPENDENTLY of the
  by-VALUE container work (`gen_container`), which is the real Phase-4 blocker.
  The two residuals are related (per-instance state) but separable.
- **De-risked plan (implementation-ready).** Add a `__present_<attr>` bool struct
  field (mirroring the proven ripple-safe `__shadow_` bool field, so struct
  equality / leaf-boxing / copy already handle it) ONLY for attrs in a new
  program-wide `deleted_attr_targets` pre-scan (parallel to `deleted_name_targets`,
  populated in the same Delete walk — `del <expr>.<attr>` → insert `attr`). Hooks:
  (a) every attribute store `x.a = ...` sets `__present_a = true` — reuse the
  `maybe_shadow_assign` call sites so coverage is complete; (b) `del c.a` sets it
  false (statement.cpp Delete/Attribute handler); (c) the convert_attribute read
  emits a conditional AttributeError when `!__present_a`. **Soundness risk to
  watch:** a MISSED store leaves it false → false ALARM (sound); a MISSED `del`
  leaves it true → false PROOF (unsound) — so the del hook must be exhaustive
  (all Delete/Attribute shapes) and validated. Deferred from THIS session only to
  avoid rushing a multi-site struct-field feature; the plan above is ready for a
  focused implementation.

## 6. Cross-references

- Master inventory: the **Class-instance identity / aliasing** and
  **Narrowing-invalidation cluster** rows in
  [python-frontend-architecture.md](python-frontend-architecture.md).
- Container precedent + perf cliff:
  [reference-semantics spike](python-frontend-reference-semantics-spike.md) and
  [dict-value-byref plan](python-frontend-dict-value-byref-plan.md).
- Backlog entry + the 2026-06-26/28 investigation notes:
  [python-frontend-plan.md §0](python-frontend-plan.md#false-proofs).


## Status update (2026-08-06): context-manager arm CLOSED

The `with` machinery's false-alarm family had TWO roots, both fixed
without the general by-reference representation change:

1. **Manager identity**: `with span:` / `with span as s:` copied the
   manager into a `__with_mgr_*` temp; `__enter__`/`__exit__` mutated
   the copy (`span.depth == 1` false-alarmed). LVALUE context
   expressions (symbol / field / by-ref param via dereference /
   container element) now bind by reference, exactly like method
   receivers; only rvalues (constructor calls, which are fresh
   objects) keep the materializing temp. By-ref params were
   additionally SILENTLY DROPPING the protocol (pointer type defeated
   the tag sniffing -- no __enter__/__exit__ at all, a missed-mutation
   false proof); they now dereference first.

2. **Loop-control over-exit** (PLR 8.5): `__exit__` was prepended to
   EVERY break/continue in the with body -- but one bound to a loop
   INSIDE the with does not leave the with suite. The inlining is now
   loop-depth-aware. This was pyhard's `span.depth == 0` failure (the
   retry loop's continue decremented depth once per iteration).

Pinned by `context-manager-identity{,-fail}` (identity through `as`,
exception-path __exit__, by-ref-param managers, nested-loop
break/continue -- all CPython-validated). The context-manager arm no
longer motivates the phased by-reference representation change; the
remaining drivers are local-alias `b = a`, composition field stores,
and return-flow (see the phase table above).


## Status update (2026-08-06): Phase 4 LANDED (opt-in, --python-ref-instances)

The heap-boxing identity fix (2026-08-05: boxed class instances
allocate a per-instance heap record via ID_allocate -- originally for
the loop-append aliasing false proof) made Phase 4 nearly free: the
existing wrap/box machinery IS the element-as-heap-pointer
representation the plan specified. Under the flag, a list display
holding class-instance elements boxes each element
(make_python_value CLASS over the heap record) and the element type
widens to python_value; iteration, subscript-attribute access and
extract-then-mutate all reach the stored object through __class_ptr,
and loop-appended elements keep per-instance identity via the same
mechanism.

Validated (all CPython-twinned): 087/099 witness shapes prove
(iterate-mutate, subscript-store, extract-mutate, loop-append);
distinct elements stay distinct (no over-aliasing); a false
post-mutation assert FAILs; flag-off default bit-identical (suite
green); the full instance-* family is verdict-identical under the
flag (14/14). The == precision cost stays as documented (boxed
elements compare by tag/value_equal, opt-in accepted).


## Status update (2026-08-10): identity spike under --python-ref-instances

The Phase-4 flag now additionally gates the representation jump the
identity family needs: CONSTRUCTION heap-allocates and binds locals
as pointers (ref_instance_locals; deref-at-use like promoted
aliases), so address = object identity, rebinding allocates fresh
(the old object stays live -- PLR 3.1), and `b = a` is a plain
pointer copy. On top of it, DEFAULT-equality class keys form
IDENTITY-KEYED dicts: keys store the pointer, lookups/membership
compare pointers (a PURE term -- the quantified witness composes),
CPython-differential battery green in both configurations. The
unconditional `is` fix (three tiers: alias-chain / pointer equality
/ sound nondet) landed alongside -- `C(1) is C(1)` no longer falsely
proves.

Spike scope: locals bound by construction and their aliases. NOT
yet under the flag's identity story: instances reaching dicts
through returns/fields/params-of-params (their conversion shapes
don't produce ref_instance_locals entries -- the guard still
rejects, fail-closed), sets, and `is` between a ref-local and a
by-ref param (tier-3 nondet today; both are pointers, comparable
once the param's deref shape is recognised alongside the local's).
Productisation = extending ref_instance_locals coverage to those
positions, then re-assessing the perf re-spike's == precision cost
with the identity representation as the default.


## Callee-side pointer returns LANDED (2026-08-10)

The return slot was the LAST by-value boundary under the flag
(params, locals, Phase-3 fields all pointers). A method whose every
Return is `self.<attr>` of ONE pointer-typed field now returns the
POINTER (maybe_pointer_field_return, shared by the free-function
and method def paths; same no-fall-through gate as the freshness
scan). The pointer TYPE of the slot is the caller-side signal: any
pointer-to-instance call result binds the target as a ref local via
plain pointer copy; chained attribute / `is` / arg-passing forms
compose through the existing deref seams. Param-passthrough
(`return t`) was probed already correct. REMAINDER (recorded):
owned-field construction identity (`self.x = Inner(1)`; two gets
returning the same object) still loud-fails -- that is the Phase-3
field-STORE-identity item (heap-allocate fresh constructions stored
into fields), not a return-path issue.


## Phase 3 COMPLETE: owned-field identity (2026-08-11)

`self.x = Inner(1)` (plain and annotated) now heap-allocates PER
EXECUTION of the store and holds the POINTER; the ctor-AS-EXPRESSION
path heap-allocates and denotes the deref of the fresh pointer, so
value contexts copy as before while pointer contexts recover
identity. This closed the documented over-aliasing trap (the shared
converted-once __ctor temp): h1.x is h2.x refutes, h.get() is
h.get() proves, no cross-instance mutation bleed, rebinding keeps
the old object live. Tests: ref-instances-owned-field{,-fail}.

## Phase 4 assessment (2026-08-11): default-on is VIABLE

Evidence, measured on this host:
- SEMANTICS: the FULL python regression suite (and the four
  regression/cbmc python tests) pass with the flag FORCED ON via a
  temporary default flip -- no test depends on by-value instance
  semantics.
- PERF: study-corpus repro sweep within noise both ways (t4 even
  improves 161ms -> 87ms); pyhard 36s on vs 35s off; suite wall
  267s (normal range). The ref_mutables nested-container == cliff
  does NOT reproduce for instance pointers.
- COVERAGE: identity flows through locals, params, self, returns
  (fresh factories AND pointer fields), owned and by-ref fields,
  is/is-not tiers, identity-keyed dicts and sets.

RECOMMENDATION: flip the default (retaining a
--no-python-ref-instances escape hatch) in a dedicated commit.
Left un-flipped pending sign-off -- a semantic default change
deserves its own review.
