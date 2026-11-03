# String refinement: guarded axioms, string lengths, and unbounded arrays

Status: design, backed by a spike (branch `string-solver-spikes`, commit
`91ff569ae6`, environment-gated). Nothing here is implemented on
`shell-strings` yet.

This document covers three changes to the string refinement solver
(`src/solvers/strings`). Each one is sound on its own:

- **G, guarded axioms.** The axioms of a string function call only constrain
  anything when the call is executed. String calls on paths that a model
  does not take then cost nothing.
- **T, tied lengths.** An array that the array pool makes for a pointer gets
  the length that the program gives that pointer, when the program gives it
  only one.
- **A, unbounded arrays.** Arrays that the array pool makes for pointers have
  an unbounded type, so that reading the model does not cut them at a size
  that has nothing to do with their length.

## 1. Background

### 1.1 How string calls reach the solver

Symbolic execution records each assignment as an SSA step with a guard (the
path condition) and `cond_expr`, the equation `lhs#n == rhs`.
`symex_target_equationt::convert_assignments` passes `cond_expr` to the
decision procedure without its guard. That is the usual SSA encoding, and it
is sound because `lhs#n` is only used where its guard holds (directly, or
through the `c ? x#1 : x#2` merges at join points).

For a string call, `rhs` is a `cprover_string_*` function application.
`string_refinementt::set_to` stores the equation. `dec_solve` then:
1. turns each call into a node of the dependency graph (`add_node` in
   `string_dependencies.cpp`), replacing the call by a fresh return-code
   symbol;
2. generates the call's constraints in `string_dependenciest::add_constraints`:
   existential lemmas, universal axioms and not-contains axioms;
3. instantiates the universal and not-contains axioms during refinement.

A string is an `array_string_exprt`: a char array, plus a length kept in the
array pool (`array_poolt::get_or_create_length`). `array_poolt::find(pointer,
length)` returns the array for a content pointer. For a pointer that no
`cprover_associate_array_to_pointer` call binds to an array,
`make_char_array_for_char_pointer` makes a fresh array symbol (a "leaf"). Its
type has the size `length` that the first `find` passed in, and its pool
length is a fresh, unconstrained symbol.

### 1.2 What goes wrong

Measured on the shell kernel `arg-loop-fail` (a `while [ $# -gt 0 ]` loop
over the arguments, unwound 10 times). Each run was repeated with 0 to 11
unconnected `x >= 0` constraints added (`DUMMY=n`), which changes only the
SAT solver's choices.

- **Dead paths.** Every unwinding contains string calls, but in a given model
  most unwindings don't run. In the final model of a converging run, 30 of
  the 52 string calls had a false guard. Their axioms still had to hold, over
  strings that the model fills with arbitrary values. In one runaway run the
  string involved had a length of 6.3e18.
- **Lengths of leaves.** For leaves, two lengths are unrelated: the pool
  length (what the solver means by the string's length) and the program's
  length field, which is also the array type's size. When reading the model,
  `boolbvt::bv_get_unbounded_array` drops the elements at or beyond the type
  size. `check_axioms` then reads a model of the array that is not the
  solver's: elements the SAT solver set are replaced by `'?'`. An axiom whose
  instance the solver satisfies stays violated at the same position, no new
  instance can fix it, and the index sets grow until memory runs out. Whether
  this happens depends on the value the SAT solver picks for the unrelated
  length.

Committed HEAD solves `arg-loop-fail` in 6 to 9 of 12 perturbed runs,
depending on the sweep. The other runs run out of memory or time out at
150 s.

## 2. Spike results

The spike implements G, T and A behind the environment variables `GUARDSTR`,
`TIEONE` and `INFLEAF`. The two tables use separate runs.

**`arg-loop-fail`, 12 perturbed runs each** (time per run at most 150 s,
memory at most 6 GB):

| Variant | Correct | Median time | Range |
|---|---|---|---|
| HEAD | 6/12 | 150 s (limit) | 25 s to out of memory |
| G | 12/12 | 11 s | 10–89 s |
| T | 12/12 | 59 s | 10–82 s |
| A | 12/12 | 51 s | 10–104 s |
| A + T | 12/12 | 21 s | 9–60 s |
| G + T | 12/12 | 10 s | 9–95 s |
| A + G + T | 12/12 | 10 s | 9–51 s |

**Full validation**: 24 shell kernels, `regression/python` (1,278 tests),
`regression/strings`, and `regression/shell` in default and refined mode.

| Variant | Kernels correct (time) | Python | Other suites |
|---|---|---|---|
| HEAD | 22/24 (79 s) | pass | pass* |
| G | 22/24 (29 s) | 1 failure, see 4.4 | pass* |
| T | 22/24 (99 s) | pass | pass* |
| A | 22/24 | `stub-json-loads-fail` times out, see 6.3 | pass* |
| A + G + T | 22/24 (30 s) | 1 failure, as with G | pass* |

\* In refined mode, three shell tests fail in every variant, because the
refined test run forces `--shell-strings refined` onto tests written for
other modes.

Further measurements:
- **Peak memory on `arg-loop-fail`**: 1.7 GB on HEAD, 260 MB with G.
- **Python suite**: the sum of test times went from 362 s to 325 s with
  A + G + T, without the test from 4.4.
- **`csv-loop` and `csv-loop-fail`**: unsolved in every variant. MiniSat on
  the quadratic Ackermann constraints dominates them, which is being worked
  on elsewhere.
- **Spot checks**: six small shell programs with known verdicts give the
  same verdict with and without A + G + T. A loop that concatenates the
  arguments ran out of memory without a verdict after 97 s (5.5 GB) on HEAD.
  With A + G + T it is `VERIFICATION FAILED` (correct) in 48 s (3 GB).

The conclusions:
- G is the main gain.
- T alone also removes the runaway. It makes the type size and the pool
  length agree, which is the root of the model-reading mismatch.
- A alone removes the runaway too, but leaves leaf lengths unbounded (see
  `stub-json-loads-fail` in 6.3).
- With G and T in place, A adds little speed. It is still the right model
  reading for leaves that T cannot tie.

## 3. Proposed order

1. **T**: medium size, solver only.
2. **A**: small, solver only, after T, so that untied leaves are the only
   ones it changes.
3. **G**: the largest change. It touches the decision procedure interface
   and goto-symex.

Each is a separate commit with its own regression tests. G could go first,
since it gives the largest gain, but it is the riskiest. T and A are needed
in any case.

## 4. Design of G: guarded axioms

### 4.1 Semantics

For an SSA step `g: lhs#n := f(args)` where `f` is a string intrinsic, the
solver asserts `g => (all constraints of the call)` instead of
`all constraints of the call`. This is sound for the same reason as the
unguarded SSA encoding: `lhs#n`, and the strings the call defines through its
output arguments, are only observed where `g` holds. The encoding is still
complete: when `g` holds, the constraints are the same as before.

### 4.2 Interface: getting the guard to the solver

`convert_guards` already makes `step.guard_handle`, a literal or constant,
before `convert_assignments` runs.

Preferred interface: a new virtual function on `decision_proceduret`:

```cpp
/// Add `lhs == rhs` (\p definition) as the definition of a fresh symbol,
/// whose uses are all under \p guard. The default ignores the guard.
virtual void set_definition(const exprt &guard, const exprt &definition)
{
  set_to_true(definition);
}
```

- `convert_assignments` calls `set_definition(step.guard_handle,
  step.cond_expr)` for every assignment.
- Every other decision procedure keeps today's behaviour, with no cost.
- `string_refinementt` overrides it. It keeps the guard only when the
  right-hand side contains a string intrinsic, and otherwise forwards to
  `set_to`.

In the spike, symex emitted `guard => equation` for every solver. That slowed
the cvc5 test `regex-split-precise-native` from 2 s to over 200 s. The
virtual function avoids that.

Fallback, if changing the `decision_proceduret` interface is not wanted: a
flag on `symex_target_equationt` (`guard_string_definitions`), set by the
goto-checkers when `refine-strings` is on. Symex then emits
`implies(guard, cond_expr)` for steps with string intrinsics, and
`string_refinementt::set_to` takes the implication apart (as the spike does).
This has more coupling: symex must know which steps the solver cares about.

### 4.3 Inside the string solver

- `string_refinementt` keeps `equation_guards` parallel to `equations`, with
  `true` for unguarded equations. Every pass that rewrites `equations` in
  place keeps the two vectors aligned. Today these passes are the
  simplification loop and `make_char_array_pointer_associations`.
- `add_node` takes a guard and stores it in a new field
  `builtin_function_nodet::guard`, which the move constructor and move
  assignment copy. The guard lives in the node because `dec_solve` can run
  several times on one solver (several properties): `add_constraints` goes
  over all nodes each time.
- `string_dependenciest::add_constraints` wraps each node's constraints, for
  testing nodes (all constraints) and for the others (`length_constraint`):
  - an existential `e` becomes `g => e`;
  - a universal axiom's body `b` becomes `g => b` (the bounds stay the same,
    and `is_valid_string_constraint` only looks at array accesses);
  - a not-contains axiom's premise `p` becomes `g && p`. The witness range
    lemmas from `instantiate_not_contains` (commit `34aca2cee8`) take the
    premise, so they are guarded too.

  Instances and counterexamples are derived from these axioms, so they carry
  the guard.
- The definitions that `dec_solve` passes to `supert::set_to` from
  `local_equations` become `g => definition` for guarded equations. This
  covers `lhs == return_code` and the `associate_length_to_array` constraint
  `length(arr) == n`. The `lhs == return_code` definition is a fresh symbol's
  definition and could stay unguarded; guarding it keeps all of a call
  together.
- `string_dependenciest::eval` returns no value for a node whose guard is
  false in the model. `string_refinementt::get` then reads the array from the
  SAT model. Otherwise `get` would compute a dead call's result from its
  inputs, which the axioms no longer require. `check_axioms` would not become
  unsound, because the axioms that use that result are guarded the same way,
  but traces would show values that are not in the model.

### 4.4 What stays unguarded

- **Calls recorded through `handle()`** (`recorded_string_applications`):
  calls inside assertions and goals, which reach the solver through
  `convert_rest` or `convert_function_application` without a step. The
  assertion encoding already guards them (`assumption => cond`). Leaving
  their axioms unguarded is today's behaviour.
- **`associate_array_to_pointer`.** It only adds the pointer-to-array binding
  to the array pool (`array_poolt::insert`, first binding wins), which cannot
  be conditional. The same is true today.
- **The pointer union-find** (`add_equations_for_symbol_resolution`). It
  merges char-pointer equations, not calls, so it is unchanged.
- **Assignment guards.** An assignment guard inside the right-hand side
  (`lhs := g' ? f(args) : lhs_prev`, from `symex_assignt`) stays a merge.
  `add_node` still extracts `f(args)`, unguarded by `g'`. The step guard
  still applies. Guarding by `g'` as well is possible later.
- **Lengths of all created strings** (`length >= 0`) and the if-length
  definitions.

### 4.5 Tests

- **Regression (refined shell)**: string calls in a loop whose unwindings
  mostly don't run (`arg-loop-fail` reduced), a `-fail` variant of it, and
  the argument concatenation loop from 2, which runs out of memory on HEAD.
- **Regression (Python)**: a string call inside an `if` whose result is only
  asserted in the other branch.
- **Unit**: `add_constraints` wraps each kind of constraint with the node's
  guard, and `eval` gives no value under a false guard.
- **Check**: the full validation shows no changed verdicts, with the
  perturbation sweep on `arg-loop-fail`, `option-split` and `path-split`.

## 5. Design of T: tied lengths

### 5.1 Semantics

`find(p, l)` stands for the string with content pointer `p` and length `l`.
The array pool has one array per pointer, and one pool length per array. If
every `find` of a leaf's pointer `p` has a length with the same value `l`,
then in every execution the string at `p` has length `l`. Asserting
`pool_length(array(p)) == l` then excludes no execution. The pool length is
internal to the solver, and only constrained by axioms about this string.

If `p` is found with lengths that may differ, `p` stands for several strings
(prefixes of one buffer, for example). The leaf then stays untied, which is
today's over-approximation.

### 5.2 Same value, decided conservatively

Lengths with the same value are found by syntax, after two kinds of
rewriting that only use unguarded SSA equations, which hold in the formula:
- **Integer copies**: `x#n == y` where `y` is a symbol, a member expression
  or a nondet symbol, kept in a `union_find_replacet`;
- **Integer merges**: `x#n == c ? a : b`, followed at most 64 steps.

Guarded equations (G) are excluded, because they only hold under their guard.

`find(c ? p1 : p2, l)`, which comes from a merged pointer:
- if `l` (after rewriting) is `c ? l1 : l2` with the same `c`, it counts as
  `find(p1, l1)` and `find(p2, l2)`;
- otherwise it counts as `find(p1, l)` and `find(p2, l)`, since it denotes
  `c ? (p1, l) : (p2, l)`.

On the shell kernels this ties every leaf: 30 on `arg-loop-fail`, 80 on
`csv-loop`, 8 to 16 on the others. On `stub-json-loads-fail` it ties 17
leaves, whose 16 apparently different lengths were copies of each other.
Before canonicalisation, 1,560 `find` calls in 37 Python tests used a pointer
with a second, syntactically different length.

### 5.3 Where

- **Recording**: `array_poolt` records each `find` (pointer, length) and each
  leaf it makes.
- **Tying**: `dec_solve` ties after `add_constraints`, once all `find` calls
  of this round are done, and before the first SAT call. It adds a lemma per
  tied leaf. `seen_instances` removes duplicates when `dec_solve` runs again.
- **Copies and merges**: they are collected in `dec_solve` from the
  unguarded `equal_exprt` equations. They are a member (kept between calls),
  like `symbol_resolve`.

### 5.4 Tests

- **Regression (Python)**: `stub-json-loads-fail` with A, which times out
  without T.
- **Regression (pointer reuse)**: a pointer found with two lengths that may
  differ (a C or Python program that shares a buffer) must stay untied, with
  the verdict unchanged.
- **Unit**: the find decomposition and the copy rewriting.

## 6. Design of A: unbounded arrays

### 6.1 Change

In `make_char_array_for_char_pointer`, a leaf's type is
`array_typet{element, infinity}` instead of `char_array_type`, unless the
size of `char_array_type` is a constant.

### 6.2 Why it is sound

Only types change. The bit-vector encoding of an array whose size is not a
constant is the same for a symbolic size and for infinity: both are
unbounded arrays. The constraints the solver generates don't use leaf type
sizes. The if-array type built in the same function uses them, but only for
model reading ("BEWARE" comment there), and `get_valid_array_size` uses them
only for constant sizes. The effect is on reading the model:
`bv_get_unbounded_array` no longer drops elements, and `get_array` cuts the
array at the pool length, as it does for fresh strings
(`array_poolt::fresh_string` already uses unbounded types).

### 6.3 Constant sizes and lengths that nothing bounds

- **Constant sizes stay.** With an unbounded type for those too, four Python
  `-fail` tests timed out (for example `func-return-dict-call-fail`).
  Arrays with a constant size are bit-blasted rather than handled by the
  array theory, and their size is an exact length.
- **`stub-json-loads-fail` timed out with A alone.** The string keys of a
  nondet dict are leaves whose pool lengths nothing bounds. In the SAT model
  they were 6.9e18. `get_array` cannot build a model for strings above
  `MAX_CONCRETE_STRING_SIZE`, so `check_axioms` kept seeing 136 violated
  equality axioms. With the old types this test succeeds, but for a reason
  that is itself a symptom of the mismatch in 1.2. In the initial order, the
  first model already satisfied all axioms. With the order of the
  `length >= 0` lemmas reversed, the first model is the same kind of model as
  with A, and refinement still converges: reading the model with the old
  types cuts these strings at their type size, so `check_axioms` sees short
  strings and accepts. Reading a model that is not the solver's can thus also
  hide violations. That can only make a counterexample spurious, never hide
  a real one, since `check_axioms` only decides when to stop refining. T
  ties these lengths to the program's (also nondet) lengths, and the test
  then takes 3.3 s with A.

A therefore goes in after T, and the leaves it changes on its own are the
ones T can't tie.

### 6.4 Tests

- **Unit**: `unit/solvers/strings/array_pool` checks the leaf types inside
  if-arrays; two of its expectations change to unbounded sizes (done in the
  spike).
- **Regression**: the `arg-loop-fail` runaway, as a reduced refined shell
  test whose model reads a leaf beyond its type size. The perturbation sweep
  is the check.

## 7. Open questions

1. **Interface for G.** Is the new `decision_proceduret::set_definition`
   acceptable, or should it be the symex flag from 4.2?
2. **Assignment guards in G.** Should G also guard by the assignment guard
   inside `if(g', f(args), lhs_prev)`? It would need `add_node` to take the
   guard from the merge.
3. **Other front-ends.** Java models are the main other user of the string
   solver. JBMC is not built here; the jbmc-strings suite should be run
   before merging G and T.
4. **Length of a merge chain in T.** The limit of 64 steps is arbitrary. The
   shell kernels need at most 10 (the unwinding).

## 8. Not covered by the spike

- `--paths`, where each path gets a fresh solver, so G matters little.
- `--incremental-loop`, which converts the equation repeatedly.
- The SMT back-ends, which don't use this solver. With the interface in 4.2
  they are unchanged.
- Performance of G when most calls are live. On `count-args-fail` and
  `int-roundtrip-fail`, every call was live in the final model, and times
  were unchanged.
