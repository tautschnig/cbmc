# Refactoring plan: `python_converter_call.cpp`

`python_converter_call.cpp` is currently **9,557 lines**, the
single largest file in the Python frontend. It contains the
implementation of `python_convertert::convert_call`, a
~9,500-line method that dispatches on:

1. method calls (`obj.method(args)`)
2. constructor calls (`Cls(args)`)
3. ~110 distinct built-in functions
4. user-defined-function calls
5. ~50 string methods
6. ~30 dict methods
7. ~20 list methods
8. lambda invocation
9. callable-instance dispatch (`__call__`)
10. nested-function invocation

This plan breaks it into smaller files by responsibility,
without changing semantics. Every rename / move is mechanical.

## Target file structure

| Source file | Approximate lines | Responsibility |
|---|---:|---|
| `python_converter_call.cpp` | 600 | Top-level `convert_call` dispatcher only — routes to the per-shape helpers below. |
| `python_converter_call_method.cpp` | 1,800 | Method dispatch: `obj.method(args)` resolution, super(), virtual dispatch via __class_tag, regex-stub fallback. |
| `python_converter_call_string_methods.cpp` | 1,300 | String methods: split, replace, format, format_map, startswith/endswith, isalpha / isdigit / isspace / etc., upper/lower, find/rfind, index/rindex, encode/decode, join, strip variants. |
| `python_converter_call_list_methods.cpp` | 800 | List methods: append, sort, reverse, pop, copy, extend, remove, index. Also bytes (modelled as list[uint8]) decode. |
| `python_converter_call_dict_methods.cpp` | 850 | Dict methods: items, values, keys, update, setdefault, pop, popitem, get, clear. |
| `python_converter_call_builtins.cpp` | 2,000 | Free-function builtins: len, range, sum, sorted, set, abs, min, max, type, isinstance, chr, ord, hex, oct, bin, hash, id, round, divmod, int, float, bool, str, bytes, list, dict, tuple, complex, map, filter, zip, enumerate, all, any, next, iter, repr, print, input, hasattr, getattr, setattr, delattr, super, classmethod, staticmethod, property. |
| `python_converter_call_nondet.cpp` | 250 | nondet_int / nondet_float / nondet_bool / nondet_str / nondet_list / nondet_dict / nondet_complex / __VERIFIER_nondet_*. |
| `python_converter_call_args.cpp` | 1,200 | Argument processing: keyword binding, *args packing, **kwargs unpacking, default value binding, type-fixup loops, annotation-mismatch checks. |
| `python_converter_call_user.cpp` | 800 | User-function-call resolution: function_aliases, lambda_returning_functions, c_intrinsic redirection, decorator wrappers, callable-instance __call__ dispatch. |

**Total: ~9,600 lines across 9 files** (currently 9,557 in 1 file).

## Mechanical extraction strategy

### Phase 1: Module / class / namespace setup

The current `convert_call` is a single method on `python_convertert`. The
class-member access pattern (`symbol_table`, `pending_checks`, `class_types`,
etc.) means the new files must remain methods on `python_convertert`.

For each new file:

1. Add a header `python_converter_call_<role>.h` that forward-declares any
   helper functions (none are needed if the helpers are member methods).
2. The new `.cpp` file `#include`s `python_converter.h` and
   `python_converter_helpers.h` (the existing shared helpers).
3. Methods that were lambdas-inside-`convert_call` become private member
   methods on `python_convertert`, declared in `python_converter.h`.

### Phase 2: Top-level signature

Refactor `convert_call`'s body to a sequence of `try_<X>` calls:

```cpp
exprt python_convertert::convert_call(const jsont &expr)
{
  // ... keep the AST-level prologue: extract func, args, kwargs ...

  // Method dispatch: obj.method(args)
  if(is_node_type(func, "Attribute"))
  {
    auto r = try_method_call(expr);
    if(r.has_value()) return std::move(r.value());
  }

  // Builtin dispatch
  if(is_node_type(func, "Name"))
  {
    std::string func_name = json_string(json_member(func, "id"));
    auto r = try_builtin_call(expr, func_name);
    if(r.has_value()) return std::move(r.value());
    auto r2 = try_nondet_call(expr, func_name);
    if(r2.has_value()) return std::move(r2.value());
    auto r3 = try_class_constructor(expr, func_name);
    if(r3.has_value()) return std::move(r3.value());
  }

  // User-function-call (with arg processing) is the fallback
  return convert_user_call(expr);
}
```

Each `try_<X>` returns `std::optional<exprt>` — populated when the dispatcher
found and handled the call, empty otherwise.

### Phase 3: Move groups one at a time

Order matters: extract leaf groups first so each commit compiles cleanly.

1. **`nondet_call`** (smallest, fewest dependencies).
2. **`string_methods`** (self-contained; relies only on `extract_string_value`,
   `python_string_literal`, `emit_string_function`).
3. **`list_methods`** (similar).
4. **`dict_methods`** (similar).
5. **`builtins`** (the largest single group; depends on the type vocabulary
   in `python_types.h` and a few helpers).
6. **`method_call`** (depends on class_types / class_bases / class_mro
   already on `python_convertert`).
7. **`call_args`** (the keyword/vararg/typecast machinery).
8. **`user_call`** (everything left).

Each step is a single commit:
- Move the relevant code block to the new file.
- Add the new file to `src/python/CMakeLists.txt`.
- Verify all three regression suites still pass.

## Class-member additions

Each `try_<X>` becomes a private method on `python_convertert`. Header
additions:

```cpp
// In python_converter.h
private:
  /// PLR §6.3.4 method dispatch. Handles obj.method(args)
  /// including super(), virtual dispatch, and regex-stub
  /// fallback. Returns nullopt if expr's func isn't an
  /// Attribute or doesn't resolve to a known method.
  std::optional<exprt> try_method_call(const jsont &expr);

  /// Built-in function dispatch (len, range, sorted, ...).
  /// Returns nullopt if func_name isn't recognised.
  std::optional<exprt> try_builtin_call(
    const jsont &expr, const std::string &func_name);

  /// nondet_int / nondet_float / etc.  Returns nullopt
  /// if func_name isn't a recognised nondet primitive.
  std::optional<exprt> try_nondet_call(
    const jsont &expr, const std::string &func_name);

  /// PLR §3.3 class instantiation: Cls(args).  Returns
  /// nullopt if func_name isn't a registered class.
  std::optional<exprt> try_class_constructor(
    const jsont &expr, const std::string &func_name);

  /// User-function-call fallback: function_aliases,
  /// lambdas, decorators, @c_intrinsic redirection,
  /// callable-instance __call__.
  exprt convert_user_call(const jsont &expr);
```

## Verification at each phase

Each move must preserve:

1. The full ESBMC sweep PASS count (currently 2601). Run after each step.
2. `regression/python`, `regression/python-strata-tests`,
   `regression/python-strata-tests-pending` — all green.
3. `git-clang-format --binary clang-format-15 HEAD^` clean.
4. The new file ordering reflects the dispatch order in `convert_call` so
   reading top-down still tracks call resolution.

## What not to do in this refactor

- **Do not change semantics.** Every code change is `git mv`-equivalent.
- **Do not introduce new helpers** unless they're already candidates
  (e.g. lambdas inside the current method that would become free helpers).
- **Do not normalise comments.** Leave each block's comments where they are
  for `git blame` continuity.
- **Do not flatten the
  `if(is_python_value_type(obj.type())) { ... if(is_python_string_type) {`
  nesting.** It mirrors the type-discriminated dispatch and the new
  per-method-type files keep the structure.

## Estimated effort

- Phase 1 (header / scaffold): ~1 hour
- Phase 2 (top-level signature): ~1 hour
- Phase 3 (8 incremental moves, each verified):
  - ~30 minutes per move on average
  - ~1 hour for the largest (builtins)
  - **Total: ~5 hours**

**Estimated total: 7-8 hours** spread across multiple commits.

## Benefits

- **Readability**: smaller files with single responsibilities.
- **Compile time**: parallel compilation of the new files.
- **Search**: faster fuzzy search; less interference from method bodies far
  from a call site of interest.
- **Onboarding**: new contributors can read each file in isolation; the
  9,500-line monolith is currently a barrier.
- **Diff size**: future changes touch a smaller blast radius.

## Drawbacks

- **Git history**: blame for every moved line shows the move commit. Mitigated
  by `git blame -C -C -C` and by keeping each move commit focused on one group.
- **Dispatcher ordering**: callers reading `convert_call` need to follow links
  to multiple files. Mitigated by keeping the dispatcher itself clear and
  short (~600 lines).

## Out of scope

- **Refactoring `python_converter_assign.cpp` (3,151 lines).** Same shape, same
  benefits, but a separate effort.
- **Reorganising `python_converter.h`'s member-state grouping.** The header
  already groups state by concern; only the new method declarations would be
  added.
- **Extracting helpers from `python_converter.cpp`.** That file's size is
  driven by helper utilities, not a single mega-method.
