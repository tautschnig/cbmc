# Closure-cell substrate — design plan (differential2 §12a)

Status: **design only, not implemented.** This is the plan for the one
remaining differential2 closure item: closures capturing variables by
*cell* (late binding) rather than by value.

## The problem

```python
fns = [lambda: i for i in range(3)]
assert fns[0]() == 2          # CPython: all three lambdas share i's cell
                              # -> the final value 2. cbmc: returns 0.
```

PLR (§4.2.2, the cell/free-variable rules): a variable that is assigned
in an enclosing scope *and* referenced by a nested function becomes a
**cell variable**. The cell is shared storage; the nested function reads
it **when called** (late binding), not when defined. So every lambda
built in the loop sees the loop variable's final value.

## Root cause (from investigation, not assumption)

Three independent mechanisms combine to break this; each must be
addressed:

1. **Comprehensions are conversion-time unrolled** into *distinct*
   per-iteration lambda symbols (`__lambda_0`, `__lambda_1`, …), each
   with the iteration value substituted — i.e. value capture, one
   closure per iteration. (`convert_list_comp`,
   `python_converter_comprehension.cpp`.)
2. **Higher-order values are stored in containers as code-typed
   elements.** `fns = [lambda…]` builds a list whose `data` array holds
   the lambda function symbols directly; calling `fns[k]()` indexes a
   code-typed slot. There is no closure *value* (function pointer +
   captured environment) that a list element can hold uniformly.
3. **Captures are read at call time from the defining scope's symbol,
   which is dead after the function returns.** Free vars are passed as
   extra call arguments read from `python::<scope>::<name>`
   (`call_user.cpp` "Add closure captures as extra arguments";
   `convert_lambda` records `closure_captures`). Function locals are
   local-lifetime, so an **escaping** closure (returned/stored, called
   later) reads a reset `0`. Within the defining function it already
   works (the symbol is live) — verified.

So §12a is not a "cell tweak"; it needs a closure *value* representation,
a cell substrate, and non-unrolled comprehensions-with-closures.

## Design: cells as boxed shared storage

A **cell** is a one-field heap/static box `{ T contents }`. A variable is
a *cell variable* iff it is (a) assigned in scope S and (b) referenced by
a nested function/lambda defined in S (CPython's exact rule; a
symtable-style pre-pass over S decides this at conversion time).

- **Cell variable storage.** Replace the plain local symbol with a
  pointer to a cell allocated once per activation of S (`__cell_<name>`).
  All reads/writes in S go through `*cell` (auto-deref at use sites, like
  the existing list/dict pointer-param handling in `convert_name`).
  Because it is one allocation reused across loop iterations, the cell
  holds the final value after the loop — giving late binding for free.
- **Closure value.** A closure becomes a struct
  `{ code *fn; cell *captures[]; }` (or a fixed-arity record). The
  nested function takes its captured cells as parameters/fields and reads
  `*cell` in the body. At definition the *cell pointers* (not values) are
  captured — shared with S and with sibling closures.
- **Escaping.** The cell outlives S (heap/static lifetime), so a
  returned/stored closure still dereferences a live cell with the last
  written value.

## Phases (each independently landable + sweep-gated)

1. **Cell-variable identification.** Symtable-style pre-pass per scope:
   `cell_vars(S) = assigned(S) ∩ free_vars(nested defs in S)`. Reuse the
   existing `collect_assigned_locals` + `collect_name_refs`. No behaviour
   change yet (just the analysis).
2. **Cell storage for non-escaping closures.** Box cell vars as
   `__cell_<name>` (static lifetime), auto-deref in `convert_name` /
   assignment. The within-function case already passes, so this is a
   refactor that must keep it passing while making the loop case
   (`for i: g = lambda: i; … g()` inside the function) read the shared
   cell -> final value.
3. **Closure value + escaping.** Introduce the closure record so a
   returned closure carries its cell pointers; calls read `*cell`.
   Replaces the value-argument capture in `call_user.cpp`.
4. **Higher-order through containers.** Give lists/dicts an element type
   that can hold a closure record (or a tagged callable handle), and
   make `fns[k]()` dispatch through it. This is the largest piece and is
   independently useful (callables stored in data structures generally).
5. **Comprehensions with closures.** When a comprehension's element is a
   closure over the iteration variable, **do not unroll**; reuse the
   §12c loop lowering (`emit_listcomp_loop`) so a single lambda captures
   the single loop cell. This makes phases 1–4 apply to the comprehension
   witness.

## PLR-correctness checkpoints

- Late binding: closures read `*cell` at call time (not at definition).
- Sharing: all closures in a loop share one cell -> identical final
  value; `nonlocal x` writes go through the same cell and are visible to
  the enclosing scope and siblings.
- Value-default capture (`lambda i=i: i`) is *early* binding and must
  stay value-captured (a default argument, not a free variable) — the
  identification pass must not treat a parameter default as a cell ref.
- Recursion/re-entrancy: a static cell is shared across activations,
  which is wrong for recursive closures; phase 3's per-activation heap
  cell (not static) is required before claiming soundness there.

## Recommended sequencing

Phases 1–2 are a contained, low-risk refactor that closes the
*within-function* loop-closure case and lays the substrate. Phases 3–4
(escaping + containers) are the heavy lift and should be a dedicated
effort with the ESBMC sweep as the regression gate (it caught the false
positives in the §12b is-bound work). Phase 5 then closes the headline
comprehension witness. Until phases 3–5 land, `closure-late-binding`
stays a KNOWNBUG.
