# CBMC Python frontend — architecture

This doc explains how the Python frontend in `src/python/`
turns a Python source file into a CBMC GOTO program. It is
intended for contributors and AI agents extending the
frontend. For verification usage see
[python-verification-guide.md](python-verification-guide.md).
For the open-work backlog see
[python-frontend-roadmap.md](python-frontend-roadmap.md).

## Top-level flow

```
   .py source
       │
       ▼
  AST (JSON via python_ast_server.py — Python's own ast module)
       │
       ▼
  python_convertert (src/python/python_converter*.cpp)
       │
       ▼
  symbol table + GOTO program (CBMC core)
       │
       ▼
  goto-symex → SAT/SMT
```

`python_languaget` (`python_language.{h,cpp}`) is the
language-API facade. It owns flags from the command line
(`--python-unbounded-ints`, `--python-check-annotations`,
`--python-lazy-stubs`, etc.), invokes the Python AST parser,
and constructs a `python_convertert` to do the heavy lifting.

`python_convertert` (`python_converter.{h,cpp}` plus
`python_converter_*.cpp`) is the converter. The .cpp files
are split by responsibility (assign / call / compare /
control / defs / except / expressions / lambda / module /
ops / statement / terms / comprehension), but they all share
the single `python_convertert` object via friend-method
patterns over a master class declaration in
`python_converter.h`.

## Pass structure

The converter runs the AST through a sequence of named
passes. Pass numbering reflects the insertion order rather
than an architectural sequence; new sub-passes have been
slotted in as design issues emerged.

| Pass | What it does | File |
|------|---|---|
| 0.1 | Resolve imports; load library models for `random`, `datetime`, etc. | `python_converter_module.cpp` |
| 0.25 | **Pre-register class types as placeholders** so type annotations referencing later-defined classes resolve to a real `struct_tag_typet` rather than a fallback | `python_converter_module.cpp` |
| 0 | Register top-level annotated globals as symbols | `python_converter_module.cpp` |
| 1a | **Convert all class definitions** (`convert_class_def`): build the full struct, register `__init__` and other method symbols, convert method bodies | `python_converter_defs.cpp` |
| 1a-bis | **Re-run** `convert_class_def` for selected classes whose method bodies couldn't fully resolve forward references in pass 1a (forward-class string annotations or self-method calls to later-declared siblings) | `python_converter_module.cpp` |
| 1b | Register all top-level function signatures (no bodies) | `python_converter_module.cpp` |
| 1b.5 | **Argument-side constant propagation** — pre-scan AST call sites and populate `string_constants[<param-id>]` for parameters whose every call-site value agrees on a constant | `python_converter_module.cpp` |
| 1c | Convert top-level function bodies | `python_converter_module.cpp` |
| 2 | Convert module-level statements | `python_converter_module.cpp` |

The non-trivial ordering is **pass 0.25 → 1a → 1a-bis → 1b →
1b.5 → 1c**. Pass 0.25 makes class-type annotations
resolvable by pass 1a. Pass 1a-bis fixes up the cases where
pass 1a's first attempt at a method body had to fall back
because a later class or sibling method wasn't yet
registered. Pass 1b registers free-function signatures so
pass 1c bodies can call each other. Pass 1b.5 walks the AST
once more, after all signatures are known, to record
per-parameter constants for the body conversion in pass 1c.

### Sub-pass 1a-bis: when does it run?

Re-running `convert_class_def` for every class is correct but
slow — for self-referential class shapes (e.g. a tree node
class whose method body cites the class itself), the second
pass forces a re-walk of nested calls without changing the
result.

The pass detector flags a class for re-conversion if it
contains either:

1. A method with a return-type annotation that is a string
   forward reference to a class **other than** the
   enclosing one. (`'Bar'` inside `Foo` triggers; `'Task'`
   inside `Task` does not.)
2. A method whose body contains `self.<name>()` calls to
   sibling methods declared **later** in source order than
   the calling method.

`convert_class_def` is idempotent under these conditions —
class_mro / class_bases dedup, and method symbols /
parameter symbols / `__ret_tmp_<class>` temps refresh their
types when their underlying class struct has grown more
fields between passes.

## Constant tracking maps

The converter maintains side-tables keyed by symbol
identifier that record what the converter has been able to
prove about each name's value at conversion time. These let
later expression conversions fold operations against known
content rather than opaquely emitting symbolic operations.

| Map | Key | Value | Populated by |
|-----|---|---|---|
| `string_constants` | symbol id (`irep_idt`) | `std::string` | string literal assignment, arg-side propagation |
| `float_constants` | symbol id | `double` | numeric literal assignment, `try_eval_double` |
| `dict_literals` | symbol id | `exprt` (struct of length + keys + values arrays) | dict literal assignment |
| `list_literals` | symbol id | `exprt` (struct of length + data array) | list literal assignment |
| `tuple_literals` | symbol id | `exprt` | tuple literal assignment |
| `complex_literals` | symbol id | `exprt` | `complex(re, im)` call |
| `dict_runtime_value_overrides` | (dict-symbol-id, key-repr) → exprt | per-key value | dict subscript-assign with type mismatch |
| `dict_literal_value_categories` | symbol id | `map<key-repr, "str"\|"int"\|...>` | dict literal assignment |
| `dict_literal_value_string_consts` | symbol id | `map<key-repr, std::string>` | dict literal with constant string values |

### Invalidation

When a name's value can change (loop iteration, branch
merge, alias write), the corresponding entry must be removed
or downgraded. The two main invalidation drivers:

- **`invalidate_loop_writes(body)`** in
  `python_converter.cpp` — called at every loop-body entry
  (`convert_while`, four `convert_for` variants).
  Recursively scans the loop body for `Assign / AnnAssign /
  AugAssign / For (target) / NamedExpr (walrus) / Tuple
  unpack` targets and clears their entries from all the
  literal/constant maps. Without this, a constant tracked
  before the loop would incorrectly fold against
  loop-modified values.

- **Subscript-assign invalidation** in
  `convert_assign` — when a non-constant subscript-assign
  is applied to a tracked dict, drop all
  `dict_runtime_value_overrides` entries for that dict
  (any key could be affected).

## Function summaries

Some optimisations need information about a function's
return shape that's only knowable after its body has been
converted. We record that information in side-tables keyed
by qualified function id:

| Map | Recorded after | Used by |
|-----|---|---|
| `function_returned_dict_keys` | `convert_function_def` finishes | dict subscript-read at call sites whose RHS is `g(...)` |
| `function_returned_literal` | first body conversion | inter-procedural dict / list / tuple literal propagation |
| `function_return_count` | first body conversion | only fold when there's exactly one return statement (multi-return functions could return distinct shapes) |
| `function_aliases` | `convert_assign` `g = h` (where h is a `code`-typed symbol) | `convert_call` at use sites of `g` |
| `lambda_returning_functions` | function body returns a lambda | lambda-binding call sites |
| `function_param_attr_uses` | scan of function body | the `--python-check-any-arg-attrs` Any-erasure detector |
| `generator_functions` | `convert_function_def` notices `yield` | `convert_call`'s `next()` builtin and `convert_assign`'s generator-instance hook |
| `generator_cursors` | `convert_assign` for `g = gen()` | `next(g)` cursor advancement and StopIteration |

## Symbol naming conventions

- **Free function**: `python::<func_name>`
- **Class**: `python::<class_name>` — the class object itself (used for type queries, `class_tag_ids[name]`)
- **Class method**: `python::<class_name>::<method_name>`
- **Method parameter**: `python::<class_name>::<method_name>::<param_name>`
- **Module-level variable**: `python::<var_name>`
- **Function-local variable**: `python::<func_name>::<var_name>`
- **Nested function** inside a parent function: `python::<parent>::<inner_func>` — the parent prefix lets us distinguish nested lambdas / closures with potentially-conflicting names. (`python_converter_defs.cpp` `qualified_func_name` builder.)
- **Synthetic temps**:
  - `__ret_tmp_<ClassName>` — the temp used by `convert_return` for `return ClassName(args)`.
  - `__cursor_<flat_name>` — generator instance cursor (PLR §6.2.9).
  - `__gen_result_<func_name>` — generator's eager-yield list.
  - `__nest_<N>` — nested-subscript snapshot in `d["a"][0] = v` rewrite.
  - `__ctor_tmp_<attr>` — constructor-call temp for `self.attr = Cls(args)`.
  - `__nondet_list_<N>`, `__nondet_dict_<N>`, `__nondet_str_<N>` — ESBMC-nondet primitives.
  - `__list_copy_<N>` — `.copy()` fresh-list temp.
  - `__sort_tmp_<N>` — bubble-sort swap temp.
  - `__rev_tmp_<N>` — `.reverse()` swap temp.
  - `__str_concat_<N>`, `__str_aug_<N>` — concat result temps.
  - `__str_int_<N>`, `__str_eq_<N>` — string-solver result temps.
  - `__shadow_<name>__vN` — name-shadow rewrite for imported-function collisions.
  - `__nondet_object_<N>` — fresh class-instance allocation.
  - `__exception_active`, `__exception_type`, `__exception_payload` — exception model.
  - `__for_idx_<var>`, `__iter_ctr_<N>`, `__for_skip_<N>`, `__for_once_<N>` — for-loop machinery.
  - `__dict_found_<N>` — dict-subscript-assign existence flag.

The naming convention is consistent enough that you can
mostly grep for `__<keyword>_` to find all sites a feature
emits a temp.

## Type system

The frontend uses a small CBMC type vocabulary to model
Python's runtime types. All defined in `python_types.h` /
`python_value_type.h`:

| Python type | CBMC type | Layout |
|---|---|---|
| `int` | `signedbv_typet{64}` | 64-bit signed; PLR's unbounded ints are gated behind `--python-unbounded-ints` |
| `float` | `floatbv_typet` (double) | IEEE 754 double-precision |
| `bool` | `bool_typet{}` | CBMC bool |
| `str` | `python_string_type()` (`struct_tag_typet` for `__CPROVER_refined_string_type`) | refinement-string struct: `{ length, data: char* }` |
| `bytes` | `python_list_type(unsignedbv_typet{8})` | list[uint8] |
| `list[T]` | `python_list_type(T)` | `struct { int64 length; T data[PYTHON_MAX_LIST_LENGTH]; }` |
| `dict[K, V]` | `python_dict_type(K, V)` | `struct { int64 length; K keys[PYTHON_MAX_DICT_SIZE]; V values[PYTHON_MAX_DICT_SIZE]; }` |
| `tuple[T1, T2, ...]` | `python_tuple_type({T1, T2, ...})` | struct of N components |
| `complex` | `struct { double real; double imag; }` with tag `python_complex` | |
| `set[T]` | `python_list_type(T)` (modelled as a list) + `python_set_set_<bool>` for unique-flagged variants | |
| `None` | int sentinel `-2^62` (`-4611686018427387904`) | matches `is None` via integer equality |
| `Any` / Union | `python_value_type()` (tagged-union struct) | `struct { tag, int_val, float_val, bool_val, str_ptr, list_ptr, ... }` |
| Class instance | `python_class_<Name>` struct | `struct { __class_tag, ...class fields... }` |

### Tagged unions for Any / Union

When a parameter or variable is typed `Any`, `T \| None`,
`Optional[T]`, or any union, we use `python_value_type()` —
a tagged union with discriminator `__tag` and per-type
fields (`__int_val`, `__float_val`, `__bool_val`,
`__str_ptr`, `__list_ptr`, `__class_ptr`). `convert_compare`,
`isinstance`, and `unwrap_value` consult `__tag` for
runtime dispatch.

`make_python_value(tag, value)` wraps a value into the
union; `python_value_int(e)`, `python_value_float(e)`, etc.
project out a specific field.

### Per-class structs

Each user `class C` materialises a CBMC struct named
`python_class_C` with components:

1. `__class_tag` (signedbv 32) — discriminator equal to
   `class_tag_ids[C]`. Used for isinstance dispatch on
   tagged-union values that hold class instances.
2. Inherited fields (from the class's MRO).
3. Class-level annotated fields (`year: int` on a stub
   class).
4. `__init__`-derived fields (`self.x = ...` writes inside
   the constructor).

Method calls dispatch on `__class_tag` for tagged-union
receivers; for direct-typed receivers (`f: Foo`), the call
goes straight to `python::Foo::<method>`.

## Loop semantics

Python `for x in iterable:` lowers to a CBMC while loop
with explicit counter:

```
__for_idx = 0
while __for_idx < iterable.length:
    x = iterable.data[__for_idx]
    [body]
    __for_idx = __for_idx + 1
```

The four `convert_for` variants in `python_converter_control.cpp`
specialise on the iterable kind:

- `range(...)` literal (often unrolled at conversion time)
- list / tuple / dict / set
- string (iterates over characters)
- generic iterable with `__iter__`/`__next__` protocol
  (PLR §3.3.1) — runs the body up to
  `PYTHON_MAX_LIST_LENGTH` iterations, breaking on
  `__exception_active` (StopIteration sets the flag inside
  `__next__`).

Every `for` and `while` body conversion is preceded by
`invalidate_loop_writes(body)` to drop any constant-tracked
values for symbols that the body may rewrite. The
`loop_depth` counter is incremented around body conversion
so string-solver helpers can havoc their SSA outputs (each
loop iteration produces a fresh `__string_len_N` /
`__string_ptr_N` instance).

## Generator semantics (PLR §6.2.9)

We use a **list-with-cursor** model rather than full
state-machine resumption. The trade-off: side-effect
ordering between yields isn't faithful (everything happens
eagerly), but for verification properties this is sound and
the encoding is dramatically simpler.

When the body of a function `gen` contains `yield`:

1. `gen` is added to `generator_functions`.
2. The body is rewritten so each `yield X` becomes
   `__gen_result_gen.append(X)` and the function returns
   the populated list at the end.
3. A call site `g = gen()` triggers
   `allocate_generator_cursor` in `convert_assign`, which
   allocates `__cursor_g` (int64, initialised to 0) and
   records the pairing in `generator_cursors[g] = cursor_g`.
4. `next(g)` in `convert_call`:
   - Checks `if cursor >= length: raise StopIteration` via
     `pending_checks` setting `__exception_active = true`
     and `__exception_type = hash("StopIteration")`.
   - Otherwise increments `cursor`.
   - Returns `data[max(cursor - 1, 0)]`.
5. `for x in g` iterates the eager list directly via the
   for-loop's own counter — the cursor is independent.

Existing exception infrastructure handles the StopIteration
propagation; `try / except StopIteration:` catches it
without further changes.

## Annotation semantics (PLR §3.1, §3.2)

Python annotations are **documentation**, not runtime
enforcement. `x: int = "hi"` binds `x` to the string `"hi"`,
not a coerced version.

The frontend reflects this in two places:

### Scalar variant (`convert_ann_assign`)

When the RHS of an `AnnAssign` has a concrete type
(`ID_struct` or `ID_struct_tag`) different from the
declared annotation, the symbol's type widens to the RHS
type. Without this, `x: int = greet()` where `greet -> str`
would `safe_typecast` the string to nondet int, losing the
actual value.

Gated on `!python_check_annotations` so the opt-in
annotation-mismatch property
(`regression/python/annotation-var-wrong-type`) still
fires when users explicitly want enforcement.

### Collection variant (`dict_runtime_value_overrides`)

Typed dicts (`d: dict[K, V]`) face the same issue at the
element level. `d[2] = "wrong-type"` into a `dict[int,
float]` cannot be stored verbatim because the values array
has a single CBMC element type. Instead:

- `convert_assign` Subscript path detects the type
  mismatch and records the original RHS in
  `dict_runtime_value_overrides[(dict-id, key-repr)]`.
- `convert_subscript` (`python_converter_expressions.cpp`)
  consults the override before the storage array. Reads
  see the actual stored value's type, so `isinstance(d[k],
  V)` reflects runtime reality.
- Non-constant subscript-assigns clear all overrides for
  the dict (any entry could be affected); type-matching
  constant assigns clear that key's override.

## Forward-class references (PLR §4.7)

A method that returns a class defined later in source
(`def bar(self) -> 'Bar'`) presents two challenges in pass
1a:

1. The annotation `'Bar'` resolves through `class_types["Bar"]`,
   which exists from pass 0.25 but is a placeholder struct
   (just `__class_tag`).
2. The method body's `return Bar(self)` needs `Bar::__init__`
   in the symbol table, which doesn't exist until pass 1a
   processes Bar later.

Sub-pass 1a-bis re-runs `convert_class_def` for the
affected classes after pass 1a is done. Three pieces have
to cooperate for the second pass to land correctly:

- `convert_class_def` is idempotent: the existing method
  symbol's type is **refreshed** (not kept) on re-entry so
  it picks up Bar's now-fully-registered struct.
- The `__ret_tmp_<class>` temp's type is refreshed
  similarly.
- `safe_typecast` extends the standard cast set with a
  pointer-to-struct dereference: `Foo*` → `Foo` returns
  `*foo_ptr`. This handles a method passing `self` (a
  `Foo*`) to a constructor expecting `f: Foo` by value.

## Exception model

Exceptions use three globals plus a per-class
discriminator:

- `__exception_active` (bool) — true if an exception is
  in flight.
- `__exception_type` (int) — `exception_type_hash(name)`,
  e.g. 20 for `StopIteration`.
- `__exception_payload` (string) — message, optional.

`raise X("msg")` sets all three; bare `raise` re-raises
without overwriting type. Runtime checks (KeyError,
IndexError, TypeError, etc.) emit the same triple via
`add_check` / `pending_checks`.

`try / except T:` matches by hashing T's name and
comparing to `__exception_type`. Exception classes
(including custom subclasses of `BaseException`) take
their hash from `class_tag_ids` plus a 10000 offset.
`except*` (PEP 654) iterates over the active type set.

`assert ¬__exception_active` is automatically inserted
after every statement (gated on
`!python_no_exception_checks`) so an unhandled exception
causes verification failure.

## Method dispatch

`obj.method(args)` in `convert_call`:

1. Resolve `obj`'s class. If `obj` is python_value (Any),
   dispatch on `__class_tag`.
2. Look up `python::<class>::<method>` in the symbol
   table.
3. If found:
   - Build `arguments`: `address_of(obj)` for self
     (cast as needed), then user args.
   - Match argument types to parameter types via
     `safe_typecast`.
   - Emit `side_effect_expr_function_callt` with the
     method's return type.
4. If not found:
   - Check `class_declared_methods[<class>]` and
     `class_types[<class>].components()` (fields).
   - If still missing and the method is not a dunder, set
     `__exception_active` to AttributeError (emit a
     dedicated `assert false` if no enclosing `except
     AttributeError`).
   - Return nondet of the expected type.

`super().method()` is special-cased:

- For `__init__`, **inline** the base method's body in the
  caller's scope. Side effects on `self` apply correctly
  (Derived's storage receives Base's writes).
- For value-returning methods, emit a **direct CALL** to
  `<base>::<method>` with the caller's self pointer
  (typecast). This avoids the `SET RETURN VALUE` from the
  inlined base body short-circuiting the caller's
  function — the bug that previously made
  `return super().get_value() + 1` return 42 instead of
  43.

## String-solver integration

For string operations the frontend doesn't constant-fold,
we route through CBMC's refinement-string solver:

- `cprover_string_concat_func(s1, s2)` — `s1 + s2`
- `cprover_string_length_func(s)` — `len(s)`
- `cprover_string_equal_func(s1, s2)` — `s1 == s2`
- `cprover_string_parse_int_func(s)` — `int(s)`
- `cprover_string_of_int_func(n)` — `str(n)` / `f"{n}"`
- (and many more in `python_converter.cpp` /
  `python_converter_helpers.h`).

`emit_string_function(func_id, operands, symbol_table,
pending_checks, in_loop)` is the helper that:

1. Allocates result symbols (`__string_len_N`,
   `__string_ptr_N`).
2. Optionally havocs them when `in_loop=true` so SSA
   gives each iteration's call-site fresh L2 indices
   (otherwise the string-solver emits conflicting
   constraints across iterations and produces UNSAT).
3. Emits the function call into `pending_checks`.
4. Returns a struct expression of result-len + result-ptr
   that downstream code can read like any other Python
   string.

When both operands are constants tracked in
`string_constants`, prefer constant-fold over the
string-solver path — the solver is correct but
substantially slower.

## Key flags

| Flag | Default | Effect |
|------|---|---|
| `--python-unbounded-ints` | off | Use CBMC bignums instead of int64; requires Z3 |
| `--python-no-exception-checks` | off | Suppress automatic `assert ¬__exception_active` after each statement |
| `--python-check-annotations` | off | Emit `annotation-mismatch` properties when an `AnnAssign` RHS type differs from the declaration |
| `--python-check-any-arg-attrs` | off | Emit `attribute-error` properties for `obj.X` accesses where `obj`'s argument-side type doesn't declare `X` |
| `--python-required-kwarg-checks` | off | Stub-completeness checks for `Required[T]` keys in `Unpack[TypedDict]` kwargs |
| `--python-check-typeddict-fields` | off | Field-type checks on PEP 448 `**kwargs` spreads |
| `--python-lazy-stubs` | off | Skip method bodies in imported stubs; signatures-only |
| `--python-smt-strings` | off | Use the SMT string theory backend instead of refinement-strings |

## Where to make changes

| Want to add... | Touch | Why |
|---|---|---|
| New builtin (like `int`, `len`, `enumerate`) | `python_converter_call.cpp` `convert_call` | One file, one switch on `func_name` |
| New AST node type | `python_converter_*.cpp` corresponding to its category (statement vs expression) | The `convert_statement`, `convert_expression`, `convert_compare` etc. dispatch on `_type` strings from the AST JSON |
| New constant-tracking map | `python_converter.h` (declaration) + `convert_assign` (population) + invalidation hooks (`invalidate_loop_writes`, alias-targets, etc.) | The map needs both ends to stay sound |
| New library stub (e.g. `random`, `datetime`, `collections`) | `src/python/library/<name>.py` | Pure Python; the converter ingests it just like user code |
| New language flag | `python_language.{h,cpp}` (declare + parse) + `set_python_*` setter on `python_convertert` | The flag needs to be threaded from CLI to the converter |
| New checker property | `add_check(cond, kind, message, loc)` from any converter file | Properties register with the frontend's `pending_checks` and surface in CBMC output |

## Tips for new contributors

- **Read `python_converter.h` first.** Most of the
  state lives there as members. The header is the
  authoritative summary of "what does the converter know
  while running."
- **Use `--show-goto-functions` and `--show-symbol-table`**
  to see what your changes produced. Especially for
  symbol-table side effects and inline GOTO.
- **`--verbosity 8`** dumps more frontend-side warnings
  (overapproximation log, missing-method lookups, etc.).
- **Constant-fold before solver-fall-through.** Most
  builtins have a constant-fold fast path; only when both
  operands are nondet/symbolic should a builtin fall
  through to the string-solver / pending_checks path.
  Constant-fold is dramatically faster and produces
  cleaner GOTO.
- **Idempotency is a habit.** Sub-passes (1a-bis), library
  re-imports, and conversion of nested classes all run
  `convert_class_def` and `convert_function_def` more than
  once. If you write code that allocates a temp or
  registers a symbol, make sure it's idempotent
  (`if symbol_table.lookup(id) == nullptr`).
- **Loop-write invalidation.** Any new constant-tracking
  map needs a corresponding `erase` call in
  `invalidate_loop_writes`. Without that, loop-mutated
  values fold against stale snapshots and produce wrong
  results.
- **Keep the AST as source of truth** for shape-level
  decisions (constants, names of class methods,
  forward-string detection). Once an `exprt` exists, the
  shape may have been smoothed by `safe_typecast` /
  `unwrap_value` and you can't recover the original.
- **Symbol-table `.value` is the function body.** When
  `convert_class_def` is called twice, the body
  overwrites — make sure the second pass produces the
  body you want.
- **Run all three regression suites** before committing:
  `regression/python`, `regression/python-strata-tests`,
  `regression/python-strata-tests-pending`.

## Verification flag suite (recap)

For real-world Python code:

```bash
ulimit -v 8000000   # 8 GB
cbmc \
    --object-bits 12 \
    --no-unwinding-assertions --unwind 3 \
    --python-no-exception-checks \
    --python-required-kwarg-checks \
    --python-check-typeddict-fields \
    program.py
```

See [python-verification-guide.md](python-verification-guide.md)
for full flag reference and tuning advice.
