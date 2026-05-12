# Design Notes for the Remaining 3 Precision-Plan Items

This document sketches the implementation approaches for
the three items that remain from the most recent 13-task
precision plan. Each needs more focused context than
the current long-running session can safely provide.

## #9 Exception groups / `except*` (PEP 654, Python 3.11)

### Scope

New language feature introduced in Python 3.11:

```python
try:
    do_work()
except* ValueError:
    handle_value()
except* TypeError:
    handle_type()
```

Semantically, the `try` body may raise an `ExceptionGroup`
(a list of exceptions). Each `except*` handler matches
only the exceptions of its declared type; unmatched
exceptions propagate in a smaller group. `raise
ExceptionGroup("msg", [ExcA(), ExcB()])` produces a group
directly.

### Current state

`TryStar` (the AST node for `except*`) routes through
the same `convert_try` as `Try`. Group semantics are NOT
understood — every `except*` effectively catches the
whole flag as if it were a plain `except`. Testing this
on PEP-654-style code either matches too broadly or
silently falls through.

### Implementation sketch

Add exception-group state to the exception-flag
machinery:

1. New symbol `__exception_group` (boolean) — set by a
   `raise ExceptionGroup(...)` call site. Indicates
   that `__exception_active` represents a group with
   multiple exception types.

2. New symbol `__exception_group_types` (fixed-size
   int array, size = PYTHON_MAX_GROUP = 8) — stores the
   type hashes of exceptions in the current group.
   Length tracked via `__exception_group_length`.

3. `raise ExceptionGroup(msg, [e1, e2, ...])`:
   - Set `__exception_active = true`.
   - Set `__exception_group = true`.
   - Populate `__exception_group_types` from the
     exception list's types; count in the length field.

4. `except* T` handler:
   - Guard entry: `__exception_active && (
       !__exception_group && __exception_type == hash(T) ||
       __exception_group && exists i <
         __exception_group_length:
           __exception_group_types[i] == hash(T))`.
   - Inside handler: set `__exception_active = false`
     ONLY if all group-types matched T; otherwise, keep
     the flag set and remove the matched types, leaving
     the remaining group to propagate.

5. The `exception_type_hash` helper is already in place
   for non-group exceptions and transfers directly.

### Scope estimate

- 60-80 lines in `convert_raise` for group detection
  and state population.
- 80-100 lines in `convert_try` (TryStar branch) for
  per-type partial-match logic.
- 30 lines of regression test covering:
  - Single `raise ExceptionGroup`.
  - `except*` catching one type, propagating another.
  - Nested `try` with partial propagation.

Total: ~150-200 lines.

## #2b Annotation-driven narrowing

### Scope

Trust callee return-type annotations when binding to
annotated variables. Example:

```python
def pick(flag: bool) -> Dog:
    if flag:
        return Dog()
    return Dog()  # both branches same class

d: Dog = pick(True)
d.breed  # should resolve to Dog.breed, not nondet
```

Today, `pick`'s inferred return type is probably Dog
since both branches agree. Narrowing-lite would also
handle the mixed case:

```python
def maybe_dog(flag: bool) -> Dog:
    if flag:
        return Dog()
    raise RuntimeError("no dog")

# Annotation says Dog; body's tagged-union stays; but
# the call site can TRUST the annotation and typecast
# the return to Dog.
```

### Implementation sketch

In the convert-call path for a user function:

1. Look up the callee's annotated return type (stored
   in `code_typet::return_type()` which we already set
   from the AST annotation).
2. If the annotated type is a concrete class-struct and
   the actual return is tagged-union, emit a typecast
   to the concrete type at the call site.
3. Similarly at the call expression level:
   `d = pick(True)` — if `d`'s annotation says Dog and
   the expression value type is tagged-union, insert
   `unwrap_value(return, Dog)` before the assign.

### Why deferred

Implementing this requires touching:
- `convert_call` to inspect the callee's declared
  return type.
- `convert_assign` / `convert_ann_assign` to honour
  the target's annotation.
- Validation that the narrowing is safe (annotation
  doesn't lie about the dynamic type).

Rough estimate: 60-80 lines. Not large, but needs
careful testing against existing regression tests to
make sure we don't introduce nondet-path explosions.

## #7 Split python_converter.cpp

### Scope

`src/python/python_converter.cpp` is 17k+ lines. Every
new feature adds 50-200 lines. Contributors reading or
bisecting the file face a growing cost. A split into
cohesive sub-files would:

- Reduce build times per change (only the touched file
  recompiles).
- Improve code review (smaller diffs per file).
- Make the implementation accessible to new
  contributors.

### Proposed split

- `python_expressions.cpp` — convert_expression,
  convert_call, convert_binop, convert_compare,
  convert_attribute (~4000 lines).
- `python_statements.cpp` — convert_statement dispatch,
  convert_assign, convert_if, convert_for,
  convert_while, convert_try, convert_match,
  convert_with (~5000 lines).
- `python_types.cpp` — type inference, annotation
  resolution, class-struct building (~2000 lines).
- `python_module.cpp` — convert_module_body, pass 0,
  pass 0.1, pass 0.25, import resolution (~2000 lines).
- `python_intrinsics.cpp` — @c_intrinsic decorator,
  fold/domain/range maps, library-function dispatch
  (~1500 lines).
- `python_converter.cpp` — entry point, convert(),
  shared state, helpers (~2500 lines).

### Why deferred

Mechanical but high-risk:
- Every function's forward declarations need to move
  to `python_converter.h`.
- Shared static helpers need to be exposed or
  duplicated.
- The CMake rule for `python_converter.cpp` needs to
  gain the new `.cpp` files.
- Static `thread_local` variables that are currently
  file-local may need to move to class members.
- Cross-file template/lambda captures need audit.

Done wrong, the split introduces subtle linker issues
or build-order bugs. Done right, it's a week of focused
work with no behavioural change — fully verified by
ensuring the 346 CORE tests keep passing after each
incremental split.

This is best done in a single dedicated session with
full context budget and `ctest --output-on-failure`
running in a watch loop. Not suitable for a
long-running autonomous session where interruptions
could leave the codebase in an inconsistent state.

## #9b Generator `.send()` / `.throw()` support

### Scope

Current state: `yield` is eagerly accumulated into
`__gen_result_<func>` list at the yield site. A
generator function returns that list as the "iterator".
`.send(value)` and `.throw(exc)` are silently ignored.

Proper support requires modelling coroutine frames:
each `yield` both produces a value AND pauses the
function, waiting for resumption. `.send(v)` makes the
yield expression evaluate to `v`; `.throw(e)` re-raises
`e` at the yield point.

### Implementation sketch

Radical restructure: convert generator functions to
a state-machine form.

1. Each `yield` becomes a labelled suspension point.
2. The generator function's body is wrapped in a `while
   True: match state: case 0: ...; case 1: ...`
   dispatch.
3. `.send(v)` sets the current yield expression's value
   and advances the state machine.
4. `.throw(e)` raises `e` at the yield point via
   `__exception_active`.

### Why deferred

This is the largest remaining item. Beyond the 300-500
lines of direct implementation work, it interacts with
nearly every feature: exception handling (throw),
closure capture (generator frame carries enclosing
scope), loop structures (yield inside for/while).

A proper implementation is a multi-week effort. It also
bottoms out against CBMC's lack of concurrency
primitives — true coroutine semantics require either
full state-machine reification (what this design
proposes) or runtime thread scheduling.

## Summary

All three items are scoped and ready for implementation
in dedicated sessions. None is blocking; all are
precision/feature extensions on top of a working
frontend.

Recommended order for future work:

1. **#2b narrowing-lite** first — smallest scope, biggest
   per-line impact for user-code verification.

2. **#7 converter split** — once narrowing lands, the
   split becomes slightly easier because the type-
   tracking code is cleaner.

3. **#9 exception groups** — largest bang for the line
   count; PEP 654 is increasingly common in modern
   Python.

4. **#9b generator send/throw** — deepest but lowest
   value for typical verification workloads; most user
   code avoids send/throw in favour of simple
   `for x in gen()`.

The current frontend already has the hooks in place for
each: `class_property_methods`, `class_mro`,
`nonlocal_names`, `pending_checks`, `dict_literals`,
etc. The remaining work plugs into these existing
structures rather than introducing new architecture.
