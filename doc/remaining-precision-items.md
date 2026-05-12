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

### Status: in progress (~38% reduction landed)

Incremental split landed across 9 commits. Main
`python_converter.cpp` reduced from 17789 to
11029 lines (38.0% reduction).

### Landed splits

| File | Lines | Contents |
|------|-------|----------|
| `python_converter_helpers.h` | 168 | Shared `static inline` helpers: `emit_string_bool_function`, `emit_string_function`, `make_nondet_string`, `double_to_floatbv`, `collect_name_refs`, `collect_param_names`. |
| `python_converter_compare.cpp` | 966 | `convert_compare` (PLR §6.10). |
| `python_converter_lambda.cpp` | 112 | `convert_lambda` (§6.14). |
| `python_converter_comprehension.cpp` | 565 | `convert_list_comp`, `convert_dict_comp` (§6.2.5 / §6.2.6). |
| `python_converter_expressions.cpp` | 750 | `convert_if_exp`, `convert_subscript`, `convert_tuple`, `convert_list`, `convert_attribute`, `convert_dict` (§6.2.x / §6.13). |
| `python_converter_ops.cpp` | 1095 | `convert_bin_op`, `convert_unary_op`, `convert_bool_op` (§6.6 / §6.7 / §6.8 / §6.9 / §6.11). |
| `python_converter_terms.cpp` | 323 | `convert_constant`, `convert_name` (§6.2.1 / §6.2.2). |
| `python_converter_control.cpp` | 747 | `convert_assert`, `convert_if`, `convert_while`, `convert_for`, `convert_return` (§7.3 / §7.6 / §8.1 / §8.2 / §8.3). |
| `python_converter_except.cpp` | 547 | `convert_break`, `convert_continue`, `convert_pass`, `convert_raise`, `convert_with`, `convert_try` (§7.1 / §7.8 / §7.9 / §7.10 / §8.4 / §8.5). |
| `python_converter_assign.cpp` | 1665 | `convert_ann_assign`, `convert_assign`, `convert_aug_assign` (§7.2 / §7.2.1 / §7.2.2). |

### Remaining in main file

- `python_convertert` constructor + core helpers
  (`json_member`, `unwrap_value`, `wrap_value`,
  `safe_typecast`, etc.).
- `convert_expression` dispatch table.
- `convert_call` (PLR §6.3.4, ~5930 lines — the
  largest remaining cohesive block).
- `convert_statement` dispatch table.
- `convert_function_def` (§8.7).
- `convert_module_body`.
- Several smaller helpers.

### Next recommended extractions

- `python_converter_call.cpp` — the 5930-line
  `convert_call` beast. Will need additional
  helpers promoted: `build_string_struct`,
  `emit_string_int_function`,
  `register_string_with_solver`. This is the
  single biggest reduction available.
- `python_converter_function_def.cpp` — the
  `convert_function_def` for user-defined
  functions (~1400 lines).
- `python_converter_module.cpp` — module-level
  passes (pass 0, pass 0.1, pass 0.25, import
  resolution).

A final dedicated session should bring the main
file under 5000 lines. At that point it would
primarily hold the entry-point dispatcher and
shared state.

### Migration pattern (for future splits)

1. Pick a cohesive function or small group.
2. Find the function's line range with `grep -n
   "^exprt python_convertert::<name>"`.
3. Check static-helper dependencies with
   `awk ... | grep -oE "(double_to_floatbv|...)" |
   sort -u`.
4. Promote any file-scope statics into
   `python_converter_helpers.h` as `static inline`.
5. Extract the function body to a new .cpp with
   the standard include prelude (`python_converter.h`,
   util headers, `python_converter_helpers.h`,
   `python_types.h`, `python_value_type.h`).
6. `cmake -S . -Bbuild` to re-glob.
7. Build. Fix any missing-include errors.
8. Run regression + integration.
9. Commit with the "Nth step" format.



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
