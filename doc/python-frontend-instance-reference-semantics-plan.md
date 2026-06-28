# Reference semantics for instances — design & phased plan

Status: **planned** (pinning suite landed 2026-06-28; implementation phased,
each phase validation-gated). This is the whole-group fix for the
**instance-identity cluster** of false proofs.

## 1. The whole-group root

Python objects have **reference identity** (PLR §3.1): binding, passing,
returning, or storing an instance does **not** copy it; all bindings refer to
the same object, and a mutation through one is visible through all. The CBMC
frontend represents an instance as a **by-value struct** in most positions, so a
mutation through one binding is invisible to the others — a **false proof**.

This single representation gap is the root of a cluster that otherwise looks like
five separate bugs:

| Manifestation | Pinning test | Copy site |
|---|---|---|
| local alias `b = a; b.x = …; a.x` | `instance-aliasing-knownbug` | local assignment (struct copy) |
| composition `self.t = t; self.t.x = …` (read via the original) | `instance-field-aliasing-knownbug`, `shared-object-aliasing-knownbug` | **field store** |
| return-flow `u = f(v); u.x = …; v.x` | `instance-return-aliasing-knownbug` | **return** |
| context-manager `with CM(v): …` mutating `v` via `self.t` | `context-manager-enter-mutation-knownbug` | field store (composition) |
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
  Return an instance by reference. Most isolated: touches the return slot typing
  + call-result binding + `convert_name` deref. No field-layout change.
- **Phase 2 — Local alias `b = a`** (`instance-aliasing-knownbug`).
  Extend `alias_targets` + `convert_name` deref to instances (the spike's
  identified two-line direction, plus locating/fixing the upstream struct-copy
  emission found in the 2026-06-26 second-round spike). No field-layout change.
- **Phase 3 — Field store / composition** (`instance-field-aliasing-knownbug`,
  `shared-object-aliasing-knownbug`, `context-manager-enter-mutation-knownbug`).
  The deepest: change instance-valued field *types* to pointers + field-store +
  `obj.f.attr` deref. This is where deep-composition `==` perf must be measured.

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
`instance-aliasing`, `instance-field-aliasing`, `instance-return-aliasing`,
`shared-object-aliasing`, `context-manager-enter-mutation`. The
concrete-class-param coercion PUN (coercion-boundary audit) is then also closed
as a corollary (the param→field copy disappears).

## 6. Cross-references

- Master inventory: the **Class-instance identity / aliasing** and
  **Narrowing-invalidation cluster** rows in
  [python-frontend-architecture.md](python-frontend-architecture.md).
- Container precedent + perf cliff:
  [reference-semantics spike](python-frontend-reference-semantics-spike.md) and
  [dict-value-byref plan](python-frontend-dict-value-byref-plan.md).
- Backlog entry + the 2026-06-26/28 investigation notes:
  [python-frontend-plan.md §0](python-frontend-plan.md#false-proofs).
