# CBMC Python frontend — architecture

This doc explains how the Python frontend in `src/python/`
turns a Python source file into a CBMC GOTO program. It is
**the** authoritative architecture reference for contributors and
AI agents extending the frontend. For verification usage see
[python-verification-guide.md](python-verification-guide.md).

The forward-looking backlog lives in two companion plans:
[python-frontend-plan.md](python-frontend-plan.md) (everything except
strings) and
[python-frontend-strings-plan.md](python-frontend-strings-plan.md)
(all `str`/`bytes`/`re` work). The **master inventory of every gap,
known soundness issue, imprecision, and performance item** is the
[Gaps, soundness issues & imprecisions](#gaps-soundness-issues--imprecisions-master-inventory)
section below — each entry links to the owning plan section, or is
marked **NO PLAN**.

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

The call-handling code is split further: `python_converter_call.cpp`
holds the user-call dispatchers, and
`python_converter_call_{builtins,nondet,string_methods,list_methods,dict_methods,set_methods,method,user}.cpp`
hold the per-category handlers (builtins, ESBMC-nondet
primitives, str/list/dict/set methods, method dispatch, and
user-function call binding).

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
3. A method that accesses `<p>.attr` where `p` is a non-self
   **unannotated** parameter (so `p` is `python_value`). Such
   `p.attr` resolves at conversion time against the
   *then-registered* classes; if the field belongs to a class
   defined **later** in the file, pass 1a baked a nondet
   over-approximation, and the re-pass (after the full 1a loop
   registered every class struct, incl. discovered dynamic
   attrs) lets it resolve. This is the *whole-group* lever for
   forward-referenced field access through a generic parameter —
   it also completes stateful data descriptors whose descriptor
   class precedes the field-owning class (`__get__`/`__set__`
   reading/writing `obj.<field>`).

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

- **Call-site global invalidation** at the single `Call`
  chokepoint in `convert_expression` — a call of *any* form
  (free function, method, transitively) may mutate a module
  global, so the scalar (`string_constants`, `float_constants`)
  and `dict_literals` entries for globals are invalidated after
  the call's arguments are converted. Scoped by a cheap
  whole-program pre-pass, `collect_function_global_mutations`,
  which records the names actually mutated inside *some*
  function/method (subscript-assign / `global` rebind /
  dict-mutating method) so never-mutated globals keep their
  folding. symex then recovers the real post-call value from
  the symbol — sound by construction. (Same invalidation
  principle as the loop case, extended to calls.)

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
| `int` | `signedbv_typet{64}`, or `integer_typet` under `--python-unbounded-ints` | 64-bit signed by default. Under unbounded ints, `integer_typet` (arbitrary precision) is used inline in typed positions; an int **wrapped into `python_value`** is boxed behind a fresh per-instance `integer*` (full precision, no aliasing) — see [Leaf boxing](#leaf-boxing-non-fixed-width-values-in-byte-imaged-aggregates) |
| `float` | `floatbv_typet` (double) | IEEE 754 double-precision |
| `bool` | `bool_typet{}` | CBMC bool |
| `str` | `python_string_type()` — the refined-string `struct_tag_typet` by default, or the native `smt_string` sort under `--python-smt-strings` | refinement-string struct `{ length, data: char* }` (fixed-width) by default; the native `smt_string` sort is **variable-width**, so a `str` stored inside a byte-imaged aggregate (`python_value.__str`, dict string keys) is **boxed** behind a typed `string*` pointer — see [Leaf boxing](#leaf-boxing-non-fixed-width-values-in-byte-imaged-aggregates) |
| `bytes` | `python_list_type(unsignedbv_typet{8})` | list[uint8] |
| `list[T]` | `python_list_type(T)` | `struct { int64 length; T data[PYTHON_MAX_LIST_LENGTH]; }` |
| `dict[K, V]` | `python_dict_type(K, V)` | `struct { int64 length; K keys[PYTHON_MAX_DICT_SIZE]; V values[PYTHON_MAX_DICT_SIZE]; }`. On the native string backend a **string** key element is boxed (`string*[]`) via `python_dict_key_elem_type` so the struct stays byte-imageable |
| `tuple[T1, T2, ...]` | `python_tuple_type({T1, T2, ...})` | struct of N components |
| `complex` | `struct { double real; double imag; }` with tag `python_complex` | |
| `set[T]` | `python_list_type(T)` (modelled as a list) + `python_set_set_<bool>` for unique-flagged variants | |
| `None` | int sentinel `-2^62` (`-4611686018427387904`) | matches `is None` via integer equality |
| `Any` / Union | `python_value_type()` (tagged-union struct) | `struct { __tag, __int_val, __float_val, __bool_val, __str, __list_ptr, __class_ptr }` — see [Tagged unions](#tagged-unions-for-any--union); `__int_val`/`__str` become boxed pointers under unbounded-ints / native-strings |
| Class instance | `python_class_<Name>` struct | `struct { __class_tag, ...class fields... }` |

### Tagged unions for Any / Union

When a parameter or variable is typed `Any`, `T \| None`,
`Optional[T]`, or any union, we use `python_value_type()` —
a tagged union with discriminator `__tag` and per-type
fields (`__int_val`, `__float_val`, `__bool_val`,
`__str`, `__list_ptr`, `__class_ptr`). `convert_compare`,
`isinstance`, and `unwrap_value` consult `__tag` for
runtime dispatch.

`make_python_value(tag, value)` wraps a value into the
union; `python_value_int(e)`, `python_value_float(e)`,
`python_value_str(e)`, etc. project out a specific field.
Scalars are stored inline; `__list_ptr` / `__class_ptr` are
opaque (`empty*`) pointers cast to the concrete container /
class struct at the use site (the indirection breaks the
self-referential type that would otherwise force `smt2_conv`
into unsatisfiable forward-reference datatype emission).

A dedicated **`CLOSURE`** variant carries callables that must flow as
runtime values: `__int_val` holds an index into the converter's
`closure_registry` and `__class_ptr` points at a heap capture record.
It backs both **fat closures** (capturing free variables) and **bound
methods** (`box_bound_method`, capturing `self`); `dispatch_closure_value`
calls the right target, prepending `self` for the bound-method entries
recorded in `bound_method_closures`.

### Leaf boxing: non-fixed-width values in byte-imaged aggregates

CBMC's byte-operator lowering (`byte_extract` / `unpack_struct` in
`lower_byte_operators.cpp`) lays a struct out at fixed byte offsets and
**requires any non-constant-width member to come last** (one only). A
struct is byte-extracted whenever it is read back through an opaque
pointer cast (`*(cast(__class_ptr, DictStruct*))`) — which is exactly how
nested containers stored in `python_value.__class_ptr` / `__list_ptr` are
unwrapped. Two value representations are **non-fixed-width**:

- the native `smt_string` sort (`--python-smt-strings`), and
- the mathematical `integer_typet` (`--python-unbounded-ints`).

Storing either *inline* inside a byte-imaged aggregate makes the
aggregate variable-width and aborts `unpack_struct` (the `github_3684`
crash class), and for ints additionally **truncates** silently to 64
bits when wrapped into the union (`2**64+5` → `5`, unsound). `byte_extract`
is fundamentally incompatible with both sorts.

The fix is uniform **leaf boxing**: a non-fixed-width leaf that would sit
inside a byte-imaged aggregate is stored behind a fixed-width **typed
pointer** to a heap object; the byte-imaged skeleton stays all-fixed-width
(so `byte_extract` is valid) and the actual `smt_string` / `integer` is
only ever touched through a clean typed dereference, never byte-imaged.
Applied at:

| leaf | field / slot | boxed type | gate | helpers |
|---|---|---|---|---|
| `str` | `python_value.__str` | `string*` | `python_smt_string_native_flag()` | `python_boxed_string_ptr_type`, `python_value_str_member_type`, `box_string_for_storage` |
| `str` | dict string **keys** | `string*[]` | same | `python_dict_key_elem_type`, `python_dict_logical_key_type`, `python_dict_unbox_key` |
| `int` | `python_value.__int_val` (incl. the `CLOSURE` fn-index) | `integer*` | `python_unbounded_ints_flag()` | `python_boxed_int_ptr_type`, `python_value_int_member_type`, `box_int_for_storage` |

Writes materialise the leaf into a **fresh per-execution heap object** (a
dynamic `ID_allocate`, via `allocate_boxed_leaf`, mirroring the closure
capture-record allocation) and store the resulting pointer; reads dereference
(centralised in `python_value_str` / `python_value_int` / `string_equal` and
per-site `python_dict_unbox_key`). Per-instance allocation is **required for
soundness**: a static per-call-site symbol would be shared across every runtime
instance of a construction site (a function returning the container, a loop), so
the boxed leaves would alias and earlier instances would observe a later one's
value (a false proof). Each `ID_allocate` execution yields a distinct object and
the container copies the pointer *value* at construction, so instances stay
independent.

Per-instance allocation alone is **not sufficient**: a tracked dict literal that
embeds a boxed leaf records the per-execution materialisation *pointer symbol*,
and the dict-subscript **constant-fold** would re-read that symbol at a later
program point — observing another instance's value (a deterministic false
proof). The const-fold therefore skips any value embedding a boxed-leaf pointer
(`contains_boxed_leaf_pointer`), falling through to the symbolic dict read,
which uses the per-instance value symex copied at construction. (Re-reading a
tracked dict-literal value that embeds *any* per-execution symbol is unsound in
general, independent of leaf type.)

With both pieces — per-instance allocation **and** the const-fold guard —
boxing is sound **and** precise for `str` (native) **and** `int` (unbounded):
a wrapped unbounded int keeps full precision (`2**64+5` is preserved, not
truncated) and does not alias across instances. Typed int containers
(`dict[int,int]` / `list[int]`) are unaffected — they store `integer_typet`
inline at full precision, never wrapped.

Every transform is a **type-driven / flag-gated no-op** on the other
back-end, so the default int64 / refined-string representations are
byte-identical (the full local suite is a 0-regression guard). Each value
type keeps the SAME boxing pattern, so a future non-fixed-width leaf is a
type-swap, not a re-architecture. Detailed rationale and the option
analysis (boxing vs. reordering vs. mutually-recursive datatypes) live in
the [strings plan](python-frontend-strings-plan.md#strings).

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
5. **Dynamically-discovered fields** — attributes written as
   `<obj>.attr = ...` on a typed parameter or a local bound to an
   instance, found by a whole-program pre-pass (`dynamic_class_attrs`,
   Pass 0.27/0.27b) and declared as `python_value` fields up front
   (CBMC structs are static). Method-shadow storage fields and their
   `__shadow_<attr>` flags (see [Attribute access](#attribute-access-descriptors--shadowing))
   are added here too.

Method calls dispatch on `__class_tag` for tagged-union
receivers; for direct-typed receivers (`f: Foo`), the call
goes straight to `python::Foo::<method>`.

### Empty-container element-type inference

A literal `[]` / `{}` / `set()` has no element/value type at the
construction site. A forward pre-scan (`collect_empty_list_inferred_types`)
walks each scope and infers them from later usage — `x.append(v)` /
`x.extend(...)` for lists, `a[k] = v` *and* `a.setdefault(k, default)`
for dicts (a `List` default yields a list value type) — populating
`empty_list_inferred_types` / `empty_dict_inferred_types`. The
construction site then builds the container with the inferred element
types instead of the `int` / `dict[str,int]` defaults, so e.g.
`a = {}; a.setdefault(1, []).append(2.0)` types `a` as `dict[int, list]`.
Inference fires only when usage is unambiguous; otherwise the default
type stands (sound).

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

### Annotation → type lowering (`convert_type_annotation`)

`convert_type_annotation` maps an annotation AST to the CBMC type used both
for the binding's runtime representation and (under `--python-check-annotations`)
as the declared type the checker compares against. Two principles matter for
soundness:

- **Unknown ⇒ `python_value` (Any/top), never `int`.** When the frontend cannot
  model an annotation precisely — a bare `range`, an unmodeled builtin, an
  unknown forward-reference, a bare `tuple` (`tuple[Any, ...]`) — it lowers to
  `python_value`. This is the sound over-approximation (the value could be
  anything) *and* it is checker-compatible (Any matches every concrete type, so
  no spurious `annotation-mismatch`). An earlier design used `python_int_type()`
  as the unknown fallback; that single collision was both a **latent
  unsoundness** (an unknown value modeled with concrete int semantics could mask
  a real bug — see the master inventory) and a **false-positive source** (the
  checker read the fallback int as a precise `int` declaration). Fixed 2026-06-26.
- **`Any`/`Union`/`Optional[container]` ⇒ `python_value`** so the runtime tag is
  tracked (see [Tagged unions](#tagged-unions-for-any--union) and the None-marker
  convention). The one remaining exception is a dict with a non-"safe" value type
  (`dict[str, Any]`, `dict[str, Optional[int]]`), which still falls back to int —
  a known checker false-positive blocked by a separate Any-valued-container
  representation issue (master inventory A; `check-annotations-any-dict-knownbug`).

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

A related forward-reference shape is **field access on a
forward-defined class through a generic parameter**: a method
`def f(self, x): return x.attr` where `x` is unannotated
(`python_value`) and `attr` belongs to a class defined later. In
pass 1a, `x.attr` resolves against the not-yet-complete registry and
bakes a nondet; the **1a-bis third re-pass condition** (see Pass
structure) re-converts such methods after every class struct is
registered, so the field read resolves. This is what makes stateful
data descriptors work when the descriptor class precedes the
field-owning class.

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

### Bound method as a runtime value (PLR §3.3.2)

A **bare** read of a method name — `m = obj.f` (not immediately
called, and not the conversion-time alias case) — is boxed as a
runtime **bound-method value**: a `CLOSURE` `python_value` whose
capture record holds `self` (`box_bound_method`, reusing the
fat-closure runtime; the registry index is recorded in
`bound_method_closures`). This lets a bound method flow through a
**container** (`handlers=[c.f]; handlers[0]()`), a **conditional**
(`m = c.f if … else c.g; m()`), or a **function return**
(`pick(c)()`) and be dispatched later via `dispatch_closure_value`,
which **prepends** the captured `self` for bound methods (vs.
appending captures for ordinary closures). The direct
`m = obj.f; m()` case keeps the cheaper conversion-time alias
(`bound_methods`/`function_aliases`). Calls to a non-Name
`python_value` callee (e.g. `handlers[0]()`) are routed through the
dispatch in `convert_call`.

## Attribute access: descriptors & shadowing

`obj.attr` **reads** resolve in this order (`convert_expression`
on an `Attribute`): `@property` getter → custom-descriptor
`__get__` (`emit_descriptor_get`) → declared struct field (with
the class-level-attr shadow-fallback ternary) → `__getattr__`
fallback → a bound-method box (if `attr` names a method) → nondet
over-approximation.

- **`@property` (getter + setter) as data descriptors.** A `@property`
  getter is invoked on a read (resolution order above). A `@prop.setter`
  is a DATA descriptor: `obj.prop = v` dispatches the setter
  (`emit_property_set`) — running its body and side effects — instead of a
  shadowing field store (`property-setter-dispatch`; closes
  a32_property_setter_side_effect / b6_property_covariant_override). The
  accessors `@prop.setter` / `.getter` / `.deleter` are converted under
  DISTINCT symbols (`prop__setter`, …) so a setter no longer **clobbers** the
  getter symbol (`python::C::prop`) — previously a class with both a getter and
  a setter had its getter silently overwritten. `class_property_setters` maps
  class → prop → setter symbol id and is resolved across the MRO.
- **Data descriptors (`__set__` / stateful `__get__`).** A class
  attribute bound to an instance whose class defines `__get__`/
  `__set__` is a descriptor (`class_descriptor_attrs`). Reads route
  to `__get__`; an assignment `c.x = v` routes through
  `emit_descriptor_set` → `desc.__set__(descriptor, obj, v)` (so the
  descriptor body runs on assignment and its invariants/side-effects
  are enforced). The instance is boxed as a CLASS `python_value` via
  the canonical `coerce_to_typed_slot` (which also sets
  `__class_tag`), so `obj.<field>` inside the descriptor method
  aliases the real instance — `__set__` storing `obj._v` and
  `__get__` reading it back share state. (When the descriptor class
  precedes the field-owning class, the 1a-bis third re-pass condition
  makes `obj._v` resolve; see Pass structure.)
- **Method shadowing.** An instance attribute that shadows a
  same-named method (`c.m = 99` where `m` is a method, tracked in
  `method_shadow_attrs`) gets a `python_value` storage field + a
  runtime `__shadow_m` flag. A bare read `c.m` dispatches via
  `if(__shadow_m) instance.m else <bound-method box>` — the
  unshadowed branch is the exact bound method (or a sound nondet if
  it cannot be boxed). Method **calls** `c.m()` are unaffected (they
  resolve via the method-call path, separate from the attribute-read
  field resolution).

## Call-signature validation

`validate_call_signature(func_key, call_ast, args, implicit_self)` is the
single chokepoint that flags PLR §8.7 call-arity errors uniformly across
**free functions, methods, and constructors**: too many positional args,
unknown keyword, missing required positional, multiple values for an
argument, and missing required keyword-only. It is driven by per-function
metadata captured at signature-registration time
(`function_max_positional`, `function_required_positional`,
`function_required_kwonly`, `function_has_kwargs`, `function_vararg_index`,
`function_signature_checkable` — set for *undecorated* signatures only).
A detected violation emits a may-raise `TypeError` (a nondet-guarded
uncaught exception), so a downstream assertion can't be vacuously proved
past a call that would `TypeError` at runtime. Bound-method values reaching
the free-function path (`m = obj.meth; m()`) are recognised via a leading
`self` parameter so their receiver-supplied `self` isn't counted missing.

## String-solver integration

There are **two string back-ends** (see the
[strings & regex plan](python-frontend-strings-plan.md#strings)):
the **refined-string** backend is the no-external-solver **default**;
the **native SMT-LIB `String`** backend (`--python-smt-strings` with
`--cvc5`/`--z3`) is **complete** (Plan A, 2026-06-12) and is the precise
option for the cases at the refined ceiling (ordering, symbol-operand
membership, slice/replace). The two share the `cprover_string_*`
intrinsic vocabulary; `--python-smt-strings` lowers them to native
`str.*` terms, the default lowers them through the refinement solver.

For string operations the frontend doesn't constant-fold, we route
(on the default backend) through CBMC's refinement-string solver:

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

## Regex (`re` module)

`re` is modelled by a shallow library stub plus `__cbmc_re_*`
SMT intrinsics, with a call-site `regex-no-match` check that
flags statically-impossible matches. The layering invariant is
that the **frontend emits refined-string arguments and the
back-end is responsible for bridging them to SMT `String`**.
Precise symbolic-subject matching needs that bridge, which the
**native SMT-String backend** (`--python-smt-strings`) provides;
on the default refined backend a negated regex match in
multi-assertion code can be slow (see `re4`/`re11` in the strings
plan). The current-state reference (what's
modelled, the backend-portability matrix, what doesn't work) is
[python-frontend-regex-story.md](python-frontend-regex-story.md);
the open work is [strings plan §4](python-frontend-strings-plan.md#regex).

## Contracts (icontract → DFCC)

icontract decorators (`@require` / `@ensure` / `@snapshot` /
`@invariant`) are routed into CBMC's DFCC contracts machinery:
the decorator's lambda body is converted as a contract clause
attached to the function. Inheritance follows Liskov — a
subclass precondition is OR-weakened against the base
(`require_else`), a postcondition AND-strengthened
(`ensure_then`) — using the class MRO. Single-level inheritance
composition works; the residual (multi-level Liskov, strict-C3
mixin precedence, async) is [plans §11](python-frontend-plan.md#icontract).

## Module & library support

Imports resolve to pure-Python library models under
`src/python/library/<name>.py`, which the converter ingests
exactly like user code (`random`, `datetime`, `math`, …). A
C-backed primitive can be marked `@c_intrinsic` so it lowers to
the corresponding CBMC/C-library routine instead of a Python
body. Unresolved imports set `__exception_active` to
`ImportError` so `try/except ImportError` composes. Open
coverage work is [plans §6](python-frontend-plan.md#modules).

## Parse daemon & performance

The AST parser can run as a persistent Unix-socket daemon
(`python_ast_server.py`, selected via `CBMC_PYTHON_SERVER_SOCKET`),
eliminating per-module Python interpreter startup — a large
win when a program imports many library modules. The frontend
also relies on `--slice-formula` (default-on) and the
`irept` sharing fast-path. The empirical per-benchmark analysis
and the slicer ↔ string-refinement contract are documented in
[architectural/python-perf-analysis.md](architectural/python-perf-analysis.md);
open optimization targets are
[plans §8](python-frontend-plan.md#performance).

## Any-erasure attribute check

`--python-check-any-arg-attrs` detects attribute-error bugs that
type-erasure hides: when a concrete-typed argument flows through
an `Any`-annotated parameter and the body does `param.attr`, the
checker (caller-side body sniff via `function_param_attr_uses`,
narrowed by `isinstance` gates) emits an `attribute-error`
property at call sites whose argument class doesn't declare
`attr`. PLR §3.3.5 isinstance-narrowing is respected.

## Key flags

| Flag | Default | Effect |
|------|---|---|
| `--python-unbounded-ints` | off | Use CBMC bignums (`integer_typet`) instead of int64; requires an SMT solver (e.g. `--cvc5` / `--z3`) — a warning is emitted if none is selected. Ints are full-precision, including when wrapped into `python_value` ([per-instance leaf boxing](#leaf-boxing-non-fixed-width-values-in-byte-imaged-aggregates)) |
| `--python-no-exception-checks` | off | Suppress automatic `assert ¬__exception_active` after each statement |
| `--python-check-annotations` | off | Emit `annotation-mismatch` properties where a declared annotation and the actual value/return/argument type are incompatible: AnnAssign RHS, reassignment after an AnnAssign, return slot, `list[T].append/insert` of an incompatible literal/name, and (provenance-gated) call arguments. Includes class-vs-class via MRO and `Union[...]` member checking. Sound when it runs (earlier CBMC-core blockers resolved); opt-in because it enforces *static* annotations, so it reports the irreducible class of real mismatches Python runs anyway (`n: int = "x"` never used as int) |
| `--python-strict` | off | Opt-in convenience **preset** enabling the static-strictness family (mypy-style) in one switch: `--python-check-annotations`, `--python-missing-return-check`, `--python-required-kwarg-checks`, `--python-check-typeddict-fields`, `--python-check-any-arg-attrs`, `--python-check-iter-none`. Additive (no default-semantics change); does **not** imply `--python-raising-ops-check` (a separate runtime-exception-soundness axis) |
| `--python-missing-return-check` | off | Emit a property at the implicit fall-through of a function with a non-`None` return annotation; fires only when that path is reachable |
| `--python-check-any-arg-attrs` | off | Emit `attribute-error` properties for `obj.X` accesses where `obj`'s argument-side type doesn't declare `X` |
| `--python-required-kwarg-checks` | off | Stub-completeness checks for `Required[T]` keys in `Unpack[TypedDict]` kwargs |
| `--python-check-typeddict-fields` | off | Field-type checks on PEP 448 `**kwargs` spreads |
| `--python-lazy-stubs` | off | Skip method bodies in imported stubs; signatures-only |
| `--python-raising-ops-check` | off | Model operations that *can* raise but whose success can't be proved (`int(str)`→`ValueError`, `os.*`→`OSError`, `re` non-str pattern→`TypeError`) as **may-raise**, instead of silently succeeding. Opt-in soundness; the default favours precision. Uses the declarative `@may_raise('Exc')` library decorator. |
| `--python-smt-strings` | off | Represent `str` with the native SMT-LIB String sort instead of refinement-strings. Requires an SMT String solver (`--cvc5`/`--z3`). Strings stored in byte-imaged aggregates (`python_value.__str`, dict keys) are [boxed](#leaf-boxing-non-fixed-width-values-in-byte-imaged-aggregates) behind a `string*` |

## Type-coercion at boundaries (PLR §3.2)

Python is gradually typed: a value of one type can be passed
through a "typed slot" — a function argument with an annotation,
a return slot with an annotation, an annotated assignment
target — and the converter must adapt the value to fit the slot.
PLR §3.2 specifies what's legal; the frontend implements the
adaptations through a family of `coerce_*` helpers.

### Boundary helpers

The converter has four boundary contexts where typed-slot
coercion happens. Each has a dedicated public helper on
`python_convertert`:

| Boundary | Helper | Used at |
|---|---|---|
| Call argument | `coerce_call_argument(arg, param_type, param_id)` | every site emitting a `side_effect_expr_function_callt` for a Python user-call (see `coerce_call_arguments` for the batch form). `param_id` drives the tag obligation (below) |
| Assignment RHS | `coerce_assign_rhs(rhs, lhs_type)` | every site emitting `code_frontend_assignt` whose LHS has a declared natural type |
| Return value | `coerce_return_value(val, return_type)` | every site emitting `code_frontend_returnt` whose value has a different type than the function's declared return |
| Container element | `coerce_element(elem, element_type)` | list / dict / set element coercion in builders and builtins (also boxes a string into a typed pointer for a boxed dict-key slot) |

All four are thin wrappers over the private
`coerce_to_typed_slot` (`src/python/python_converter.cpp`),
which hosts the shared PLR rules. The boundary-named helpers
exist solely so that every PLR call-site is grep-able by intent
("what kind of boundary is this?"). When PLR ever specifies
divergent rules per boundary, only the relevant public helper
changes.

**Identity preservation across an `Any` boundary (PLR §3.1).** When
`coerce_to_typed_slot` boxes an instance into a `python_value`/Any slot, it must
preserve object identity so a callee mutation is visible to the caller. It boxes
`address_of(expr)` for ANY persistent lvalue — `symbol`, `dereference` (an
aliased instance pointer `*r` from `r = obj`), `member` (`obj.field`), or `index`
(`lst[i]`) — not just a plain symbol. An RVALUE struct (a fresh `V()`) keeps the
materialise-a-copy path (a new object, identity irrelevant). Without this, `f(r)`
where `r = obj` boxed a throwaway copy and lost the callee`s mutation (a2; see the
Class-instance-identity inventory row).

### None marker convention

PLR §3.2 says `None` is the single value of `NoneType` and may
flow through any typed slot via the gradual type system. When
a `python_value{NONE}` value reaches a typed slot, the converter
rewrites it to a per-target-type marker rather than letting the
generic `safe_typecast` / `unwrap_value` path produce a
NULL-deref or wrong-field-extraction:

| Target type | Marker | Recognised at compare site by |
|---|---|---|
| `python_string` | `{length=0, data=NULL}` | data-pointer == NULL (distinguishes None from `""`) |
| `python_int` (signedbv, integer) | `python_none_sentinel_int()` cast to target | `(x == sentinel)` |
| `python_float` (floatbv) | sentinel cast to IEEE double via `ieee_floatt::from_integer` | `(x == sentinel_f)` |
| `python_list` / `python_dict` / `python_set` / `python_tuple` | `safe_zero(target)` (length-0 marker) | n/a — `Optional[container]` lowers to `python_value` at the typing layer (see below) |
| `python_value` | leave as-is (the slot is already tagged-union) | tag check (`__tag == NONE`) |

`Optional[list]`, `Optional[dict]`, `Optional[set]`,
`Optional[tuple]`, `Union[T, None]` for any container T, and
`T | None` for any container T, all lower to `python_value`
at the typing layer in `convert_type_annotation`. This is the
PLR-clean encoding because the empty-container marker
`safe_zero(...)` is structurally indistinguishable from `[]`,
`{}`, `()`, or `set()` at compare sites — only the tagged-
union form has a discriminator. Container-typed `coerce_to_typed_slot`
still emits `safe_zero` markers as a defensive fall-back for
any non-Optional container slot that somehow receives a None
value, but in well-formed PLR-typed code the boundary helpers
route those through `python_value` instead.

The recognizer accepts BOTH the literal struct form
(`is_python_none_constant(arg)`) and a symbol-expression whose
stored value is `python_value{NONE}` — this is how the
imported-module pre-pass freezes per-`FunctionDef` defaults at
module scope into static `__def_<func>_<idx>` symbols (see
`python_converter_module.cpp`). Without the symbol-form path
the defaults loop would bind the wrong field for
`Optional[T] = None` cases.

### Any / union tag obligations (TypeError on a wrong runtime tag)

Python annotations are **not** runtime coercions. When a tagged-union /
`Any` value (`python_value`) is *used as* a concrete type and its runtime
tag does not match, CPython raises `TypeError` — but the unwrap path
(`unwrap_value`, which reads e.g. `__int_val`) would silently extract the
wrong field. The frontend emits a runtime **tag obligation**
(`python-type-error` property: `assert tag(v) ∈ expected`) at three sites so
that mismatch is caught instead of silently producing a wrong value:

| Site | Where | Rule |
|---|---|---|
| **Binary arithmetic operator** | `convert_bin_op` (`python_converter_ops.cpp`) | a `python_value` operand of `+ - / // % **` whose runtime tag is non-numeric raises `TypeError` (the other operand is a concrete numeric; `Mult` is excluded — `str/list * int` is repetition). Fires on the actual tag, so a genuinely-numeric value never false-alarms. |
| **Shift / bitwise operator** | `convert_bin_op` | `<< >> & \| ^` accept only `INT`/`BOOL` operands (stricter than arithmetic — float and str both raise). A `python_value` operand against a concrete int/bool raises `TypeError` unless its tag is `INT`/`BOOL` (`union-shift-bitwise-typeerror`; closes the b3/b5 inherited-mutation cases). |
| **Both operands tagged-union** | `convert_bin_op` | for the *strictly*-numeric ops `- / // **` (where NO non-numeric operand is ever valid — unlike `+` concat, `%` str-format, bitwise set-ops), `a OP b` with BOTH operands `python_value` raises `TypeError` if EITHER tag is non-numeric (`union-both-operands-typeerror`; closes a12, two union fields retagged to str). |
| **Subscript** | `convert_subscript` (`python_converter_expressions.cpp`) | a concrete non-subscriptable scalar receiver (`int/float/bool`) is a **definite** `TypeError`; a `python_value` receiver must carry a container tag (`STR/LIST/DICT`) or `CLASS` (may define `__getitem__`). |
| **str-only method on a non-str** | method dispatch (`python_converter_call_method.cpp`) | a str-only method (`upper`/`lower`/`strip`/… — NOT `count`/`index`, which list/tuple share) on a CONCRETE non-str built-in receiver (`int/float/bool/complex/list/tuple/dict/set`) raises `AttributeError` (`str-method-on-non-str`). Gated to avoid FPs: excludes `python_value`/Any (could be str), `str`/`smt_string`, **bytes** (modelled as `list[uint8]` — it HAS these methods), and user-class instances (may define the name). |
| **Call argument** | `coerce_call_argument` (`python_converter.cpp`) | binding a `python_value` arg to a concretely-typed **scalar** parameter requires the arg's tag to match (int: `INT`/`BOOL`; float: `INT`/`BOOL`/`FLOAT` per the PEP 484 numeric tower; str: `STR`). **Gated on annotation provenance** (below). |

**Annotation provenance** (`explicitly_annotated_params`, a `std::set<irep_idt>`
of parameter symbol ids) is the architectural enabler for the call-argument
obligation. An unannotated parameter defaults to `python_value` (Any) but is then
often overwritten by a *call-site-inferred* concrete type — so a bare `int`
parameter type may be a genuine `def f(x: int)` annotation **or** an inferred
type for a parameter (e.g. a `lambda`) that truly accepts Any. The set records
only the former (`!annotation.is_null()` at def time). The obligation fires only
for explicitly-annotated scalar params; inferred/default scalar params are
excluded, so e.g. `keep([A(),B()], lambda e: True)` does not false-alarm. The
parameter identifier (`python::<qualified_func>::<param_name>`) matches
`params[i].get_identifier()` at every call site. The set is populated for BOTH
free functions (`convert_function_def`) and **methods** (`convert_class_def`,
which has its own param-processing path) — without the method-side population the
provenance gate would skip *every* method-argument check (it then over-skipped a
genuine `calc.multiply(5, "ten")` mismatch). The same provenance set now also
gates the `--python-check-annotations` **call-argument annotation-mismatch**
check (not just the tag obligation), which is what removed the lambda/`*args`/
inferred-param checker false positives (2026-06-26). It remains reusable for
future return-/assign-boundary obligations.

Related: `unwrap_value` to a `float` target **promotes** an `INT`/`BOOL`-tagged
payload (`__int_val` → float) rather than reading the unset `__float_val`
(numeric tower; guard `unwrap-int-to-float-promotion`).

### Coercion-boundary audit: which slots pun a `python_value` (2026-06-26)

A whole-group audit of every boundary where a `python_value` (union/Any) value
can cross into a *typed slot*, probing whether a wrong-tagged value is silently
punned into a concrete field (a false proof) vs. kept tag-bearing (sound):

| Boundary | Behaviour | Status |
|---|---|---|
| Call parameter (concrete scalar) | provenance-gated tag obligation | sound (precision-bounded: a function that *ignores* a mismatched param is not flagged — Python checks no annotations at the call) |
| Return slot | **widens to `python_value`** when a path genuinely returns one | sound + precise (#2) |
| Dict value (unannotated `{}`) | values are `python_value` → tag preserved → use-site obligation fires | **sound** |
| **List element** (`list[int].append(u)`) | the concrete int element type **puns** the `python_value` (coerced via `coerce_element`/unwrap, losing the tag) | **OPEN false proof** — needs slot-widening |
| **Attribute field** (`self.x: int; o.x = u`) | the concrete field type **puns** the `python_value` | **OPEN false proof** — needs slot-widening (the a2/a3 narrowing cluster) |

**The architectural invariant the audit reveals:** a slot typed `python_value`
(Any) *preserves* the runtime tag, so a later misuse is caught by the
operator/subscript/call tag obligations — it is sound. A slot with a *concrete*
type (`list[int]`, `field: int`) drops the tag on store, so a wrong-tagged value
is read back at the concrete type and a misuse does not fault. The principled fix
for the two OPEN rows is therefore **slot-widening**: type the container element
/ struct field as `python_value` when a `python_value` is stored into it (the
same move the return slot now makes). This is **invasive + perf-costly** (it
changes container/struct element typing, with the precision/perf cost that drove
the concrete-typing design and the not-viable `--python-ref-mutables` default),
so it is deferred; the two cases remain KNOWNBUG (`shared-object-aliasing` and
the a2/a3 field-narrowing witnesses). Tag obligations are NOT a default-mode fix
here: storing a mismatched value is legal Python (the error arises on a later
*use*), so a store-site assert false-alarms on values that are never misused
(the `greet(42)` lesson).

### Why have a separate "boundary" abstraction?

The first version of the frontend used `safe_typecast`
everywhere, which doesn't know about boundary semantics. The
generic path emits goto code that's "sound by accident" —
NULL-deref of an opaque container pointer (`__list_ptr` /
`__class_ptr`) produces nondet which CBMC sometimes
treats as nondet (correct for verification, wrong for "is
None") and sometimes as undefined behaviour (sound but
fragile). A test would pass or fail depending on which
counter-example the solver happened to find first.

Centralising boundary adaptations into named helpers means:

1. **Each PLR rule has one home.** `is None` semantics for
   typed slots, `Optional[T] = None` defaults binding, return-
   value None-marker — all in `coerce_to_typed_slot`.
2. **Future PLR adaptations land in one place.** Adding a new
   target-type None marker means changing `coerce_to_typed_slot`,
   and every caller (call args, assign RHS, return value,
   container element) gets the fix simultaneously. The
   `Optional[container] → python_value` lowering at the typing
   layer is the architectural escape hatch for cases where the
   natural-type marker is ambiguous (`list/dict/set/tuple`
   None markers conflate with empty literals).
3. **Boundary-specific rules can diverge cleanly.** If PLR ever
   specifies different semantics per boundary, the named
   wrappers diverge while the shared body stays the same.

### Class-constructor sequence

A related architectural pattern: every place that needs to emit
a `ClassName(args)` call goes through one helper:

```cpp
auto init_call = build_class_init_call(class_name, self_lvalue, call_node, loc);
if(init_call)
  block.add(code_expressiont{*init_call});
```

`build_class_init_call` in `python_converter.cpp` does the full
PLR §9.3 sequence in one place: MRO walk for `__init__`
(`lookup_init_via_mro`) → `address_of(self_lvalue)` as first
arg → positional + keyword arg conversion → default padding →
`coerce_call_arguments` for boundary adaptations → emit
`side_effect_expr_function_callt`. Currently used by
`convert_call`, `convert_assign` (Attribute and Name targets),
`convert_for` class init in convert_control, and the with-stmt
context-manager init in `convert_except`.

### "Soundness via NULL-deref" anti-pattern to avoid

Whenever you add a new typed slot or a new boundary site, watch
for code shape like:

```cpp
arg = safe_typecast(arg, param_type);
```

…where `arg` could be `python_value{NONE}` and `param_type` is
a natural type (str, int, float, list, dict). The generic
`safe_typecast` will route through `unwrap_value`, which for
non-int targets emits a NULL deref (`*((typed *)NULL)`) or
reads the wrong field (`__int_val` = 0 instead of sentinel).
The symex may treat this as nondet, masking the bug.

The fix is to use the appropriate boundary helper instead:

```cpp
arg = coerce_call_argument(arg, param_type);   // call boundary
arg = coerce_assign_rhs(arg, target_type);     // assignment boundary
arg = coerce_return_value(arg, return_type);   // return boundary
```

`safe_typecast` is still the right tool for non-PLR contexts:
intermediate type-coercion inside an expression, internal
representation conversions, etc. — anywhere there is no typed
slot semantics involved.

## Gaps, soundness issues & imprecisions (master inventory)

This is **the** honest, current inventory of where the frontend
deviates from the Python Language Reference or is incomplete. Every
entry links to the section in the [plan](python-frontend-plan.md) or
[strings & regex plan](python-frontend-strings-plan.md) that addresses
it, or is marked **NO PLAN**. Ground rules: every deviation is **sound**
(over-approximation / precision miss, never a false proof) **unless
explicitly flagged**; bounded-container and 64-bit-`int` limits are
intrinsic design choices, not bugs. The tables are grouped by kind:
soundness, imprecision, performance, intrinsic.

### A. Soundness (false proofs / latent unsoundness / deliberate tradeoffs)

The **common-case** default configuration has no known false proofs —
the 2026-06-18/19 audit plus the cross-module global-dict (`R1`) and the
call-signature fixes closed the ones that were found. **However, differential
testing against an external CPython-semantics corpus (2026-06-25) DID surface
open false proofs in advanced-feature corners** — the "narrowing-invalidation"
cluster (see the dedicated row below); they are now pinned as `*-knownbug`
regression tests. So the honest headline is: the common-case config is clean,
and the remaining false proofs are confined to advanced/dynamic features, each
tracked by a KNOWNBUG test. The 2026-06-24 work
closed two further latent-unsoundness classes: (i) the **side-effecting
operand call-duplication** class — a `python_value`-returning operand
referenced twice in a lowering (tag predicate + payload unwrap, or a
membership container) re-evaluated a side-effecting call, diverging the
two copies → false proof; fixed by materialising such an operand **once**
in `python_converter_compare.cpp`; and (ii) the **leaf-boxing aliasing**
class — the string/int [leaf boxing](#leaf-boxing-non-fixed-width-values-in-byte-imaged-aggregates)
first materialised each boxed leaf into a *static per-call-site symbol*, so a
container built more than once (function return / loop) aliased its leaves
across instances (a false proof in the equality direction, and it superseded
the pre-existing `--python-unbounded-ints` 64-bit truncation). Both are fixed
by **per-instance heap allocation** plus a **const-fold guard** (the dict-literal
subscript const-fold must not re-read a boxed leaf's per-execution pointer); the
root was the const-fold, not the allocation, so strings and unbounded ints are
both sound *and* precise. The const-fold guard was then **generalised**
(`value_is_const_foldable`) to the whole class — re-reading any tracked
dict-literal value that embeds a mutable symbol (not just a boxed leaf) was
unsound (`n=1; d={"k":n}; n=2; d["k"]`). Finally, the last documented open false
proof — **extraction-then-mutate** (`r=c[i]; r.append(x)`) — was closed by an
invalidate-on-mutation guard (havoc the source container; see the
nested-mutable-aliasing row). The **2026-06-25** work (sound-mode + numeric/type audit, plus differential
testing) landed several soundness fixes and recorded several KNOWNBUG residuals.
Fixes: **definite integer overflow** in the default 64-bit model now reports
`python-model-bound` instead of silently wrapping (`10**19`, `1<<70`,
big-literal arithmetic; `int-overflow-literal-reported`); **`--python-unbounded-ints`**
(the sound int mode) is now genuinely sound for **shifts** and **bitwise `&|^`**
(both previously truncated to 64-bit — leaks in the mode that promises
soundness; `unbounded-bitwise-soundness`); **subscript** of a non-subscriptable
value raises `TypeError` (`any-subscript-typeerror`); the **call-argument tag
obligation** now fires for explicitly-annotated scalar params via annotation
provenance (`param-coercion-typeerror`); **`del obj.attr`** on an instance-only
attribute havocs the slot to nondet instead of leaving a stale concrete value
(`del-attr-no-stale-value`); **`unwrap_value` → float** promotes an INT/BOOL
payload (numeric tower; `unwrap-int-to-float-promotion`). Audited **clean** (no
false proofs, with lock-in tests): bytes / complex / Decimal
(`numeric-model-soundness`), and type / `isinstance` / virtual dispatch /
`hasattr` (`type-dispatch-soundness`).

The **2026-06-26** work closed a further **latent unsoundness** and reduced
`--python-check-annotations` false positives. `convert_type_annotation` had
overloaded `python_int_type()` as both the legitimate `int` type AND the
"annotation I cannot model" fallback (a bare `range`, an unmodeled builtin, an
unknown forward-ref, a bare `tuple`). Modeling an *unknown-typed* value with
concrete `int` semantics is a latent unsoundness — and it was real: switching the
fallback to `python_value` (Any/top, the sound over-approximation) made the
by-value corpus sweep *gain* `ethereum_bug-fail` (the int fallback had been
masking a genuine bug). The same change removed the matching checker
false-positives (the checker had read the fallback int as a precise `int`
declaration). Lock-in: `check-annotations-unknown-is-any`. The other 2026-06-26
work was **precision-only** (no soundness change): the call-argument
annotation-mismatch checks are now **provenance-gated** (fire only for genuinely
annotated scalar params — `explicitly_annotated_params`, extended this turn to
method parameters), removing the lambda/`*args`/inferred-param false positives;
and the annotation-strictness family was packaged behind the opt-in
`--python-strict` preset (it is **not** default-on — see inventory B).

**Standalone PLR re-audit (2026-06-26)** — a differential pass (≈255 generated
probes run under both CPython and cbmc, flagging every program where CPython
raises/asserts-false but cbmc verifies). It **fixed one correctness false proof**
and **surfaced five new, narrow ones** (each now pinned KNOWNBUG):
  - **CLOSED — float floor division `//`** was computed as plain *true* division
    (no floor): `7.0 // 2.0` proved `== 3.5` instead of `3.0` (a wrong-arithmetic
    false proof). Fixed by applying the float-floor encoding (same as float `%`);
    lock-in `float-floordiv-correct`. By-value sweep 0 regressions.
  - **CLOSED (2026-06-26) — extraction-then-mutate guard bypassed by an
    annotation**: the `r = c[i]; r.append(x)` guard (havoc the source) is sound,
    but `r: list = c[i]` (annotated) skipped it — the AnnAssign path did not
    record `extracted_container_alias`. Now recorded on that path too
    (`annotation-extract-mutate-guarded`).
  - **CLOSED (2026-06-26) — method-call argument tag obligation gap**: the
    provenance-gated call-arg tag obligation fired for free functions but **not
    method calls** (the main method path used raw `safe_typecast`). The method
    param-binding now routes through `coerce_call_argument`, so an `Any`-tagged
    `str` bound to an `int` method param is caught (`method-arg-tag-obligation`).
  - **OPEN — missing TypeError for non-numeric operand shapes** (false negatives
  - **CLOSED (2026-06-26) — missing TypeError for non-numeric operand shapes**:
    a concrete **float operand to a bitwise/shift/invert** operator (`1.0 & 2`,
    `1.0 << 2`, `~1.0`) and **sequence × float** repetition (`"abc" * 2.5`,
    `[1] * 2.0`) now raise TypeError via the whole-group operand-type obligation
    (`float-bitwise-typeerror`, `sequence-mul-float-typeerror`).
  - **OPEN — positional-only param passed by keyword** (`def f(x, /, y); f(x=1)`)
  - **CLOSED (2026-06-26) — positional-only param passed by keyword** (`def
    f(x, /, y); f(x=1)`) now raises TypeError (`positional-only-kwarg-typeerror`)
    — recorded per-function (`function_posonly_params`) and checked in the shared
    `validate_call_signature`; with **all five re-audit items now closed**, only
    the per-instance-identity / aliasing cluster remains.
  The audit also **re-confirmed** the documented clusters still reproduce
  (composition aliasing, union dispatch, context-manager mutation, list/attr PUN,
  `*args`+kwonly, `int()`/`float()` ValueError default-off).

The entries below are the deliberate
soundness-vs-precision tradeoffs and the guarded / opt-in cases.

**2026-06-28/29 — differential-oracle arc (baseline 22 → 8).** A sustained pass
against the external CPython-semantics corpus closed a series of false proofs,
each landed with a regression test, full suite green, by-value sweep 0
regressions, and oracle 0-NEW: **reference-semantics-for-instances** (the whole
instance-identity cluster — return/alias/field — plus the `a2` annotated-alias /
lvalue-into-Any identity extension); **binop eval-order** (`g() + x` left-operand
sequencing); **`@property` setters** as data descriptors (a32/b6, + the
getter-clobber fix); **operand-type obligations** extended to shift/bitwise
(b3/b5) and both-union strictly-numeric binops (a12); **tuple-unpack arity**
(ty-015); **method mutable-default sharing** (a10); **`str`-method on a concrete
non-str** → AttributeError; **`dict.get`/`pop`/`setdefault` default type**
(`value|type(default)`, ty-005); and **`--python-check-annotations`** coverage +
precision (element/dict-value boundaries; ABC-union + inferred-container FP
fixes; default-on measured ~1.25% FP and declined); **fixed-tuple slicing**
(`t[1:]` returns a correct tuple — a real correctness bug); and **`del` +
`__getattr__` retype** (c4/laurel-006: a `del self.x` in a `__getattr__`-class
now stores the fallback`s result so the cross-type use is caught). The **6
remaining** oracle items are: DEEP representation work — **c2** (dict
structural-mutation through a call); the **annotation-laundering family caught
under `--python-check-annotations` with default-on declined** — **004**/**007**
(arg/element) and **ty-010** (return annotation, *not* a variable-length-tuple
gap); and other settled — **binop-mirror-evalorder** (pinned KNOWNBUG) and **d1**
(numeric-tower, out-of-subset) — see the rows below.

**Differential audit (2026-06-29).** A targeted probe batch (under-exercised
sequence/container/builtin/operator/dunder edges, CPython vs cbmc default) found
7 new false proofs. **Fixed (whole-group):** a type-EXCLUSIVE method on the wrong
concrete receiver → AttributeError (`(5).append`, `[1].keys`, `(1,2).add` —
generalises the str-method check; `method-on-wrong-type`); fixed-length
**list-unpack arity** (`a,b,c=[1,2]` → ValueError; `list-unpack-arity`); and a
constant **tuple index out of range** (`(1,2)[9]` → IndexError; `tuple-index-oob`)
— each 0 sweep regressions. **Pinned (KNOWNBUG + oracle corpus
`plr-audit-2026-06-29`):** `sum(["a","b"])` (TypeError), `for i in 5` (TypeError),
`set().pop()` (KeyError), `[1,2] < [1,"a"]` (mixed-type ordering TypeError). The
oracle baseline therefore grew 6 → 10 (4 newly-tracked false proofs). Also noted:
`import sys` alone spuriously emits an uncaught-exception (a false POSITIVE — sound
direction — to investigate). **P1 scope-spike (instance structural-mutation through
a call):** an instance is by-reference through a parameter (a field write
propagates), and del/setattr + `__getattr__` through an ANNOTATED param
(`clr(o: F)`) already works (`getattr-after-del-param`); the open case is an
UNannotated/Any param (the class is not statically resolvable for dispatch),
which unifies with c2 (class/structural resolution across a call boundary).

**Differential audit round 2 (2026-06-29)** — new areas (exceptions, comprehensions,
f-strings/format specs, dunder-protocol violations, str/%-format ops, builtins).
Found 14 false proofs. **Fixed:** subscripting a class with no `__getitem__` in
its MRO → TypeError (`subscript-no-getitem`); `divmod(a, 0)` → ZeroDivisionError
(`divmod-by-zero`) — both 0 sweep regressions. **Pinned (KNOWNBUG +
oracle corpus, clusters):** dunder-protocol-missing (`for x in C()` with no
`__iter__`/`__getitem__`), `__len__` returning non-int/negative, %-format /
format-spec / f-string type errors (`"%d" % "x"`, `"{:d}".format("hello")`,
`f"{s:d}"`), `str.join` of non-str, `ord(multichar)`, `round(str)`,
`sorted(incomparable)`, `str.encode(bad-codec)`, dict-comprehension unhashable
key. The oracle baseline grew 10 → 22 (12 newly-tracked). These cluster into a
few whole-group opportunities for future work: **builtin arg/edge type-checks**
(ord/round/join/sorted/encode), **format-spec validation** (the `%`/`{:d}`
family), **dunder-protocol-missing → TypeError** (DONE for the main sites — see below),
and **`__len__`/`__index__` return validation**.

**Dunder-protocol-missing cluster (closed for the main sites, 2026-06-29).**
A whole-group: an operation that requires a protocol dunder on a concrete class
instance that does not define it (across its MRO) raises TypeError. Now covered:
subscript (`obj[k]` needs `__getitem__`), call (`obj()` needs `__call__`), and
**iteration** (`for x in obj` / `[.. for x in obj ..]` needs `__iter__`, or
`__getitem__` for the old sequence protocol) — the for-loop and single-generator
comprehension sites emit the not-iterable TypeError, gated on
`class_mro_defines` so a `__getitem__`-only sequence class (and an `__iter__`
returning a separate iterator object) is NOT flagged. Tests `iterate-no-iter`
and `comprehension-no-iter` (CORE). **Residual:** the assign-unpack site
(`a, b = C()`, `[*C()]`) is a structurally different handler, pinned
`unpack-no-iter-knownbug`; and a `__getitem__`-only class is correctly not
flagged but its iteration is still modelled imprecisely (zero iterations).

**Cluster roadmap progress (2026-06-29, cont.).**
- *Iteration-protocol-missing: CLOSED across all three sites.* Added the
  assign-unpack site (`a, b = C()` -> TypeError, `unpack-no-iter` CORE) alongside
  the for-loop and comprehension sites. The `class_mro_defines(__iter__) &&
  class_mro_defines(__getitem__)` gate + a type-error assert is now the shared
  whole-group lever across for-loop / comprehension / unpack. (Residual: the
  star-in-list-display `[*C()]` is a separate path, minor.)
- *Dunder-return-contract validation (type axis): whole-group via a helper.*
  `dunder_return_type_violation(sym, kind)` flags a dunder whose
  declared/inferred return type concretely violates its contract (Any never
  flagged). Applied to `__len__` (int) and `__str__` (str); `__index__` was
  already caught. Tests `dunder-len-nonint-type`, `dunder-str-nonstr` (CORE).
  *Residuals:* `__bool__` (bool vs int not reliably distinguishable in the repr
  -> a strict check risks FP), `__hash__` (not dispatched at all -- custom-key
  hashing unmodelled), and the `__len__` NEGATIVE-VALUE case (a value check that
  would FP on symbolic-but-nonneg returns -> `dunder-len-nonint-knownbug`).
- *Builtin arg/edge preconditions: an irreducible per-builtin FAMILY, not a
  single whole-group.* Each member has a distinct predicate (ord: one-char
  string; divmod: nonzero divisor; round: numeric arg; join: all-str elements;
  sorted: mutually-comparable; encode: valid codec). The SHARED lever is
  `emit_conditional_exception(predicate, exc)` gated on a CONSTANT operand (so
  symbolic operands are never falsely flagged). Done: `divmod` (ZeroDivisionError)
  and `ord` (single-char TypeError, `builtin-ord-multichar` CORE). The rest
  (round/join/sorted/encode) remain tracked in the oracle corpus as individual
  family members.
- *Format-spec validation (`"%d" % x`, `"{:d}".format(...)`, `f"{x:d}"`): deep,
  pinned.* Requires modelling format-string parsing and per-conversion type
  rules; tracked in the oracle (`fmt_*`) and as `str-percent-format-type-knownbug`.

Net effect of the two audit rounds + roadmap: oracle false-proof baseline
6 -> 22 (discovery) -> 19 (after the iteration cluster, `__len__`/`__str__`
return-type, and `ord`/`divmod` fixes).

**Roadmap execution (2026-06-30).**
- *P3 re-triage.* The 3 ty-unsoundness laundering witnesses (004 arg / 007
  list-element / 010 return boundary) are default-mode false proofs that
  `--python-check-annotations` CATCHES (verified). They are the annotation-trust
  design boundary -- intrinsic to default mode, mitigated by the opt-in flag,
  already locked by the `annotation-*` / `check-annotations-*` CORE tests. The
  oracle gained an `ORACLE-INTRINSIC` marker so such witnesses are excluded from
  the default-mode soundness debt. Honest debt: 19 -> 16 (+3 intrinsic).
- *P0 builtin-edge family (element-type / arg / iteration axes).* Closed:
  `sum([...])` non-numeric element type, `",".join([...])` non-str element type,
  `round(str)`, and `for x in <scalar>` (int/float/bool not iterable). All gate
  on a CONCRETE element/arg type (Any/symbolic never flagged). The shared lever
  is the constant element/arg type + `emit_conditional_exception`. CORE tests
  `builtin-sum-nonnumeric`, `builtin-join-nonstr`, `builtin-round-str`,
  `iterate-non-iterable-scalar`. Debt 16 -> 12.
- *P1 audit round 3* (exceptions / with / generators / decorators / numeric):
  found 6 false proofs. *Fixed (whole-group):* the **context-manager protocol**
  (`with C()` where C lacks `__enter__`/`__exit__` -> TypeError) -- this EXTENDS
  the dunder-protocol-missing cluster (subscript/iteration/call) to `with`,
  reusing `class_mro_defines`. CORE `with-no-enter`, `with-no-exit`. *Pinned*
  (oracle corpus `plr-audit-2026-06-30`): non-callable decorator, decorator
  wrapper-arity mismatch, generator `.send()` before priming, `int(math.inf)`.
- *Remaining builtin-edge members* (`sorted` mixed-comparable, `set().pop()`
  empty-set repr, `str.encode` bad-codec, `comp_key_unhashable` dict-comp
  key-hashability, `list_compare_mixed`) need element-iteration/categorisation,
  empty-set bitmap, a codec whitelist, or loop-var typing -- moderate, tracked.

Net 2026-06-30: oracle 19 -> 16 (P3) -> 12 (P0) -> 16 (round-3 discovery of 4
new). The dunder-protocol-missing whole-group now spans subscript, iteration
(for/comprehension/unpack), call, and the context-manager protocol.

**Protocol-completeness sweep (2026-06-30).** A probe confirmed 7 more
unhandled protocol sites, so the dunder-protocol-missing whole-group was closed
systematically via a factored helper `concrete_class_lacks_dunder(type, dunder)`
(true iff a concrete `python_class_*` receiver lacks the dunder across its MRO;
false for builtins / Any / non-class, inherited dunders respected). New sites:
`obj[k]=v` (`__setitem__`), `del obj[k]` (`__delitem__`), `x in obj`
(`__contains__`, gated on ALSO lacking `__iter__`/`__getitem__`), unary
`__neg__`/`__pos__`/`__invert__`, and binary operators (`__add__`.. with the
reflected `__r op__`: after both dispatches fail, a concrete user-class operand
that cannot handle the op and whose partner is not Any -> TypeError; builtins
never reflect-handle a user class). CORE tests `setitem-no-dunder`,
`delitem-no-dunder`, `contains-no-dunder`, `unaryop-no-dunder`,
`binop-no-dunder`. `await` (`__await__`) is wired but inert (async results'
types are not surfaced -- sound, harmless). The whole-group now spans:
subscript-read/-store/-delete, iteration (for/comprehension/unpack), membership,
call, context-manager, unary, and binary operators -- ALL on the one
`class_mro_defines` lever. Validated: each site flips to FAILED on a class
lacking the dunder; valid/inherited/builtin/Any/reflected cases SUCCESSFUL; suite
green; sweep PASS 2719, 0 regressions; oracle 0 NEW false proofs, no new false
alarms. **Bonus:** the sweep resolved the long-standing deep finding
`c2_typeddict_del` (cross-call `del` invalidating `in`-narrowing) -- it now
correctly raises at the post-`del` read. Oracle debt 16 -> 15 (plus ~6 latent
protocol false proofs closed that were never in the corpus).

**Remaining protocol site:** ordering comparisons (`<`/`>`/`<=`/`>=` with no
`__lt__`/`__gt__`/... ) -- the compare handler interleaves list/string/sequence
ordering, so the missing-dunder hook needs careful placement to stay FP-free;
tracked. (`==`/`!=` must stay unflagged -- they have identity defaults.)

**P1 closes (2026-06-30).**
- *Hashability mini-group (whole-group).* A dict key / set element must be
  hashable; the built-in mutable containers list/dict/set are unhashable (PLR
  §3.2). Factored `is_unhashable_type()` and applied it at the previously
  unchecked sites (the `d[k]=v` store already had it): dict literal `{[1]: 2}`,
  dict comprehension `{k: 1 for k in [[1]]}` (closes `comp_key_unhashable`), and
  `set.add`/`set.discard`. Tuples and user classes (hashable by default) are
  never flagged. CORE `dict-literal-unhashable-key`, `dictcomp-unhashable-key`,
  `set-add-unhashable`.
- *int() float-domain cluster.* `int(float('inf'))` / `int(math.inf)` ->
  OverflowError; `int(float('nan'))` -> ValueError (constant operands only;
  symbolic floats not flagged). Closes `int_inf`; `int("3.5")` was already
  caught. CORE `int-float-inf`, `int-float-nan`.
- Oracle debt 15 -> 13. All: suite green, sweep 2719/0-reg, 0 new false alarms.

*P1 remaining (tracked):* `str_encode_bad` (needs a codec-name whitelist),
decorators (`dec_not_callable` -- decorator value not callable; `dec_wrong_arity`
-- wrapper-arity mismatch, in the complex HOF/wrapper handler), plus the
deeper/harder items: `sorted_incomparable` + `list_compare_mixed` (mixed-element
comparability), `set_pop_empty` (empty-set bitmap not provably 0), `len_nonint`
negative-value, the format-spec family (`fmt_*`), `gen_send_before_start`, and
the numeric/operator-order pair (`d1_numeric_tower`, `binop_mirror_evalorder`).

**P0a + P1 triage (2026-06-30, cont.).**
- *Ordering protocol completed.* `<`/`<=`/`>`/`>=` between user-class instances
  whose MRO defines none of `__lt__`/`__le__`/`__gt__`/`__ge__` (nor the
  reflected form) now raises TypeError, reusing `concrete_class_lacks_dunder`
  (`==`/`!=`/`is` untouched). The dunder-protocol-missing whole-group now covers
  **subscript r/w/del, iteration (for/comprehension/unpack), membership, call,
  context-manager, unary, binary, AND ordering**. CORE `ordering-no-dunder`.
  (A defensive element-wise mixed-category check was added to the list-lex
  `element_lt`, but mixed list literals box the element to `python_value`, so
  `list_compare_mixed`/`sorted_incomparable` still need boxed-element concrete-
  type tracking -- deferred.)
- *Precision-debt triage (the 265 false alarms).* Categorised: ~143 are
  unprovable assertions (mostly unsupported-feature sound over-approximations --
  acceptable), and ~101 are `python-attribute-error` false positives, almost all
  in `laurel-encoding-soundness` and **dominated by `@dataclass` field access**:
  the attribute-error tracker flagged every bare annotation not assigned by an
  EXPLICIT `__init__`, but a `@dataclass`'s synthesised `__init__` assigns them
  all. **Fixed** the spurious AttributeError (a `@dataclass` folds its bare
  annotations into the constructor-assigned set; sound, 0 new false proofs).
- *@dataclass `__init__` synthesis (DONE 2026-06-30).* Modelled the synthesised
  `__init__` so construction BINDS constructor args to field values:
  `build_dataclass_init_block` records the annotation-ordered fields at class
  definition and, at the construction sites (assignment + expression
  constructor), binds each positional/keyword arg to its field (omitted-with-
  default left at the class default) and sets the class-level-attr shadow flag
  so reads pick the instance value. Gated to a `@dataclass` without an explicit/
  inherited `__init__`. Sound (mirrors CPython field order/defaults): suite
  green, sweep 2719/0-reg, oracle 0 NEW false proofs, **false ALARMS 265 -> 231
  (-34, the @dataclass field-value cluster)**. CORE `dataclass-construct-fields`.
- *Unified construction (2026-06-30).* Factored `build_class_construction` (the
  __init__ call, else the @dataclass binding, else empty) and routed ALL four
  construction sites through it -- assignment, expression constructor,
  `return ClassName(...)`, and with-manager -- so @dataclass binds uniformly
  everywhere. The `return`-constructor path was heavily used by dataclass
  witnesses: false ALARMS **231 -> 206 (-25)**. CORE `dataclass-return-construct`.
  **Cumulative @dataclass precision: 265 -> 206 (-59), 0 new false proofs.** The
  remaining ~206 false alarms are dominated by instance-reference-semantics
  conservatism and unsupported-feature unprovable assertions, not scattered
  cheap FPs.

**Comparison whole-group + P1 triage (2026-06-30, cont.).**
- *list_compare_mixed CLOSED.* `[1,2] < [1,"a"]` -> TypeError via static
  lexicographic evaluation of the two constant lists (per-element
  `orderable_category_of` + a normalized order-key that recovers a boxed
  python_value's static tag/value), flagging a numeric-vs-str element comparison
  ONLY when the earlier positions are provably equal (so `[5,2] < [1,"a"]` is
  not flagged). With `sorted` and `element_lt`, the comparison/ordering
  whole-group is complete. CORE `list-compare-mixed`.
- *str_encode_bad CLOSED.* `str.encode`/`bytes.decode` with an unknown CONSTANT
  codec (normalized) outside a generous standard-encodings whitelist ->
  LookupError; symbolic never flagged; sweep-clean. CORE `str-encode-bad-codec`.
  Oracle false-proof debt **12 -> 10**.
- *dec_not_callable DEFERRED:* needs def-time decorator-APPLICATION modeling
  (the frontend doesn't apply a general `@d` -> `f = d(f)`, so there's no site
  to check `d`'s callability) -- awkward, parked.
- *P1 triage of the 54 `ast-bytecode-lowering` false alarms:* NOT a whole-group.
  They are a LONG TAIL of individual subtle-semantics imprecisions -- call/arg
  evaluation order (008), def-time default-argument timing (014), with-LIFO
  (018), walrus escape, cell promotion, class-scope opacity, annotation scopes,
  ... -- each a distinct point issue, plus 6 `no-body` unsupported stubs. So the
  cheap-discovery loop found NO new dataclass-sized cluster here. **The one
  remaining big precision whole-group stays the perf-gated container-element
  instances** (§5b); `ast-bytecode-lowering` is a low-priority long tail.

**Format-spec whole-group COMPLETE (2026-06-30).** A shared
`format_code_violation(code, value_category, percent_style)` +
`value_format_category` enforce conversion/presentation-code vs value-type
compatibility at ALL three sites: f-string (`FormattedValue`), `str.format`, and
`%`-format (str `Mod`). `%d`/`%f` on a str -> TypeError; `{:d}` on a str/float ->
ValueError; `{:s}` on an int -> ValueError. Gated on a constant spec + concrete
value (symbolic/Any, `%s`/`%r`/`%a`, bare and width-only specs never flagged).
Closes `fmt_pct_type`, `fmt_spec_d_on_str`, `fmt_fstring_d_str`; oracle
false-proof debt **10 -> 7**. CORE `format-spec-{percent,strformat,fstring}`.
*Separate PRE-EXISTING false alarm noted:* `"%d" % <int/float>` spuriously
raises an uncaught exception in the str-`Mod` path (not introduced here; the
str-%-format result production is itself imprecise) -- tracked.

**Decorators (`dec_not_callable`, `dec_wrong_arity`) DEFERRED -- needs a real
feature.** The frontend does not model GENERAL decorator application (`@d` is not
lowered to `f = d(f)`; only special decorators -- icontract/c_intrinsic/property/
overload -- are recognised, others ignored). Closing these two requires modelling
the application: (a) at def time, if a decorator value is a concrete non-callable
-> TypeError (`dec_not_callable`); (b) tracking the wrapper's signature so a call
through the decorated name with mismatched arity -> TypeError (`dec_wrong_arity`).
Both need def-time emission across `convert_function_def`'s many return paths plus
wrapper-signature plumbing -- a dedicated decorator-application effort, not a
point fix.

| Area | Issue | Status | Plan |
|---|---|---|---|
| Float floor division | `//` on float operands was computed as true division (no floor): `7.0 // 2.0 == 3.5` — wrong arithmetic | **CLOSED 2026-06-26** (float-floor applied; `float-floordiv-correct`) | — |
| Extraction-then-mutate via annotation | `r: list = c[i]; r.append(x)` bypassed the extraction guard (AnnAssign path skipped `extracted_container_alias`) → stale container proved | **CLOSED 2026-06-26** (AnnAssign path now records the alias; `annotation-extract-mutate-guarded`) | — |
| Method-call arg tag obligation | the provenance-gated call-arg TypeError obligation fired for free functions but not method calls (main method path used raw `safe_typecast`) | **CLOSED 2026-06-26** (method param-binding routed through `coerce_call_argument`; `method-arg-tag-obligation`) | — |
| Non-numeric operand TypeErrors | a concrete float operand to `& \| ^ << >> ~`, and `seq * float` repetition, were silently modelled instead of raising TypeError | **CLOSED 2026-06-26** (whole-group operand-type obligation; concrete-float-only so set bitwise is unaffected; `float-bitwise-typeerror`, `sequence-mul-float-typeerror`) | — |
| Positional-only by keyword | `def f(x, /, y); f(x=1)` was not flagged as a TypeError | **CLOSED 2026-06-26** (`function_posonly_params` + `validate_call_signature`; `**kwargs` correctly absorbs; `positional-only-kwarg-typeerror`) | — |
| Shift / bitwise on a union operand | `<< >> & \| ^` accept only int/bool, but a str/float behind a tagged union (an inherited mutating method retags a field, read via `self.field >> 1`) was silently shifted | **CLOSED 2026-06-29** (int-only operand obligation, stricter than arithmetic; `union-shift-bitwise-typeerror`; closes b3/b5) | — |
| Both-union strictly-numeric binop | `a - b` (or `/ // **`) with BOTH operands tagged-union (two fields retagged to str) emitted no obligation | **CLOSED 2026-06-29** (both-union obligation for strictly-numeric ops only — `+`/`%`/bitwise excluded since concat/str-format/set-ops are valid; `union-both-operands-typeerror`; closes a12) | — |
| str-only method on a concrete non-str | `(5).upper()`, `[1,2].upper()`, `{}.strip()` were silently accepted instead of `AttributeError` | **CLOSED 2026-06-29** (`str-method-on-non-str`; gated — not `count`/`index`, not Any/`str`/bytes=`list[uint8]`/user-class). The **Any-erasure variant (ty-010)** is OPEN below | — |
| Tuple-unpack arity | `a, b = (1,)` silently skipped the missing `_1` field instead of raising `ValueError` | **CLOSED 2026-06-29** (fixed-tuple arity mismatch is a definite ValueError, gated on no `*starred` target; `tuple-unpack-arity`; closes ty-015) | — |
| Method mutable-default sharing | a method`s mutable default (`def add(self, items=[])`) was copied fresh per call, so the shared-state accumulation (and the threshold-retag exploit) was lost | **CLOSED 2026-06-29** (method mutable defaults frozen into a static-lifetime symbol with a once-only module-init; annotated defaults accumulate precisely and share across instances; `method-mutable-default-shared`; closes a10). UNannotated method defaults bind via a separate Any path and do not yet accumulate (pre-existing precision limitation, not a regression) | — |
| dict `get`/`pop`/`setdefault` default type | the `default` was coerced to the dict`s value type (`{}.get("k","s")` returned int `0`, not `"s"`), so a later `+ 1` missed the str `TypeError` | **CLOSED 2026-06-29** (result type is `value_type \| type(default)`: a provably-absent key returns the default in its own type, a present constant key the value, the symbolic case a `python_value` union; `setdefault` also widens the empty-dict value type via inference; `dict-get-pop-default-type`, `dict-setdefault-default-type`; closes ty-005) | — |
| binop eval-order **MIRROR** | `x + g()` where `g()` mutates a value read by the LEFT operand: cbmc reads `x` AFTER `g()` (hoisted) rather than before (PLR left-to-right), so a wrong value can false-prove. The FORWARD case `g() + x` is fixed | **OPEN** (pinned `binop-mirror-evalorder-knownbug`); needs left-operand-read materialisation, narrowly gated to avoid the recursive-unwind regression the broad version caused | [plan §0](python-frontend-plan.md#false-proofs) |
| Variable-length-tuple slice → str method (ty-010) | `def f(t) -> str: return t[1:]` then `f(...).upper()`: the **`-> str` return annotation is trusted**, so `f(...)` is typed `str` and `.upper()` is accepted even though the body returns a tuple/list | **Annotation-laundering family (same as 004/007), flag-gated.** Caught under `--python-check-annotations` — the *return*-annotation-mismatch check fires on `return t[1:]` (a sequence vs declared `str`). DEFAULT trusts the annotation (declined for the same measured ~1.25% benign-FP reason as 004/007); pinned `vartuple-slice-strmethod-knownbug`. **NOT a variable-length-tuple-model gap** (a fixed-tuple slice is now modelled exactly — see "Fixed-tuple slicing"; a `tuple[int,...]` sequence model is a separate *precision* item, not required to close this) | [plan §7](python-frontend-plan.md#check-annotations) |
| dict `del` through a call (c2) | `del p["x"]` inside `rm(p)` does not propagate the deletion to the caller`s dict, so a later `pt["x"]` misses the `KeyError` | **OPEN, deep**: dicts are by-reference for EXISTING-key value modifications but NOT for STRUCTURAL mutations — adding a key or deleting one does not cross the call boundary (keys/length arrays not shared); same representation limit as dict-value-byref | [dict-byref](python-frontend-dict-value-byref-plan.md) |
| `del obj.attr` + `__getattr__` fallback (c4/laurel-006) | after `del self.x`, access falls to `__getattr__` returning a different type; the concretely-typed field could not hold it, so `del` havocked the slot to nondet (sound for a stale read, but it missed the cross-type `TypeError`) | **CLOSED 2026-06-29** (`getattr-after-del`): a field `del self.x`-ed in a method of a `__getattr__`-class is typed `python_value`, and `del` stores `__getattr__`'s result into the slot, so the cross-type use routes through the operand obligation and raises `TypeError`. The deletable-field scan is module-wide, so both `del self.x` (in a method) and a direct `del f.x` (external instance) are covered. Present-int read / del-then-reassign / non-deleted field / no-`__getattr__` class all correct. **Residual:** `del o.x` through an Any-boxed FUNCTION PARAMETER does not propagate (the del handler cannot resolve the param's class) -- the structural-mutation-through-a-call family (c2) | [plan §10](python-frontend-plan.md#descriptors) |
| **Narrowing-invalidation cluster** (CLOSED 2026-06-28) | a value's runtime type changes via an effect cbmc does not model, then it is used at the stale type → CPython `TypeError`, cbmc verifies. Distinct roots (**not one fix**). **CLOSED:** `__setattr__`/`__getattribute__` (2026-06-25, over-approx to nondet `python_value`); **enum `.value` after a member retag** (2026-06-28: heterogeneous-enum value type is `python_value` + `.value` resolves on enum-typed variables AND fields, so the retagged value routes through the operand/tag obligations — `enum-value-after-mutation` is now CORE). **CLOSED 2026-06-28 (reference-semantics-for-instances):** context-manager `__enter__`/`__exit__` field mutation AND composition aliasing were the SAME *concrete-class-typed slot copies the instance* root, now fixed (instance fields are by-reference) (a `t: SomeClass` param/field is value-copied, so a mutation through it is invisible to the original; the Any-typed path preserves identity by-address). Also OPEN: inheritance+union virtual dispatch, same-expression **eval-order × union-retag** | partly **UNSOUND** (the reference-semantics false proofs remain), confined to advanced/dynamic features; the reference-semantics cases AND the eval-order case (`union-use-after-mutation-typeerror`, 2026-06-28: a side-effecting binop left operand is now sequenced before a right read so a same-expression union retag is observed) are now CORE. **Fully CLOSED (2026-06-28).** The only residual is the *mirror* eval-order case (`x + g()` where the side-effecting operand is on the RIGHT and mutates a value read on the LEFT) — pinned `binop-mirror-evalorder-knownbug` (the forward-only sequencing fix does not cover it; the reverse needs left-operand-read materialisation, narrowly gated to avoid the recursion-unwind regression the broad version caused) | [plan §0](python-frontend-plan.md#false-proofs) |
| Any/union used at a wrong type | a tagged-union/`Any` value used as a concrete type with a mismatched runtime tag now raises `TypeError` via **tag obligations** at the operator, subscript, and (provenance-gated) call-argument boundaries — was a silent wrong-field read. The remaining hole is the call-argument obligation only firing for **explicitly-annotated** scalar params (inferred/Any params excluded to avoid false alarms) | sound (closed for the three covered sites); see the tag-obligation table in [Type-coercion at boundaries](#any--union-tag-obligations-typeerror-on-a-wrong-runtime-tag) | [plan §0](python-frontend-plan.md#false-proofs) |
| Definite integer overflow (default 64-bit) | the default 64-bit model silently **wrapped** on a statically-provable >64-bit result (`10**19 < 0`, `1<<70 == 0`) — a false proof. Now reports `python-model-bound` (assert+assume cut) on a DEFINITE overflow; a symbolic/computed overflow remains the documented 64-bit bound (use `--python-unbounded-ints`, now sound incl. shifts/bitwise) | sound (definite cases reported; symbolic = documented bound) | [plan §0](python-frontend-plan.md#false-proofs) |
| Unknown annotation fallback | `convert_type_annotation` lowered an annotation it could not model (bare `range`/`tuple`, unmodeled builtin, unknown forward-ref) to `python_int_type()`, modeling an unknown value with concrete int semantics — a latent unsoundness that could mask a real bug | **CLOSED 2026-06-26**: unknown ⇒ `python_value` (Any/top), the sound over-approximation (sweep gained `ethereum_bug-fail`). Lock-in `check-annotations-unknown-is-any`. **Exception still open:** a dict with a non-"safe" value type (`dict[str, Any]`) still falls back to int — an opt-in `--python-check-annotations` false positive (not a false proof), blocked by a separate Any-valued-container capacity-model-bound issue; pinned `check-annotations-any-dict-knownbug` | [plan §9](python-frontend-plan.md#precision) |
<!-- 2026-06-26: the blocker is the SYMBOLIC-KEY dict precision cluster, not a standalone capacity bug -- an Any dict value makes d.get(k,...) a nondet key, so a later d2[key].append(...) over-approximates and fires a spurious python-model-bound (github_3684 class). -->
| Class-instance identity / aliasing | a class instance was value-semantics (struct copy) at a top-level `b = a` assignment, so a mutation through one alias was invisible to the other. Distinct from the list/dict case, which already aliased (the `alias_targets` pointer mechanism). | **CLOSED 2026-06-28** (reference-semantics-for-instances, Phases 1-3): instances are by-reference at returns, local aliases, and fields -- so return-flow, local alias `b=a`, AND composition/field store all preserve identity. `instance-return-aliasing`, `instance-aliasing`, `instance-field-aliasing`, `shared-object-aliasing`, `context-manager-enter-mutation` are all CORE. **Extended (a2):** an ANNOTATED alias `r: C = o` now pointer-promotes too (`convert_ann_assign`), and an lvalue instance crossing an Any/`python_value` boundary preserves identity by `address_of` for ALL lvalue forms (symbol / dereference / member / index), not just a plain symbol — so `f(r)` where `r=o` no longer passes a throwaway copy (`instance-annotated-alias-call`; closes a2_narrowing_alias). Soundness: a FRESH construction stays owned/by-value (distinct, no over-aliasing). The deep-composition `==` perf cliff did NOT materialise, so this is default-on (unlike container ref-semantics) | [instance ref-semantics plan](python-frontend-instance-reference-semantics-plan.md) + [plan §0](python-frontend-plan.md#false-proofs) |
| Nested mutable-element aliasing | anonymous nested-mutable list/dict elements stored by value; the replication / self-append / new-container channels are **guarded** (report `python-model-bound`). **Direct** nested mutation works (`c[i].append(...)` for lists, and int-keyed dict values via lvalue value slots). **Extraction-then-mutate** (`r=c[i]; r.append(...)` / `v=a[k]; v.append(...)`) is **guarded** in the default config (2026-06-24): the extracted variable is recorded (`extracted_container_alias`) and, on a subsequent in-place mutation, the source container is havoced + dropped from the const-fold maps (sound over-approximation). **For LISTS, the principled whole-group fix is now implemented behind opt-in `--python-ref-mutables`** (2026-06-24): nested list elements are heap-allocated per instance and aliased by pointer (`make_python_value(LIST, allocate_boxed_leaf(rebuild_list_as_pv(e)))`), so extraction / multi-instance / reorder / membership / nested value reads / 3-deep composition are **precise**; under the flag the havoc guard is skipped for wrapped references. | sound for the CONTAINER-ELEMENT aliasing channels (guarded). **However a distinct aliasing channel — object-identity-via-COMPOSITION (one object referenced by two attributes, mutated through one and read through the other) — is an OPEN false proof in the default config**, pinned `shared-object-aliasing-knownbug` (value semantics for the composed object does not track the shared identity). With `--python-ref-mutables` the nested-list cases below become precise (validated: §4 gate green, 5 genuinely-false negatives stay FAILED, default suite green). **Confirmed OPT-IN ONLY (2026-06-25):** making it the default is **not viable** — precise nested-list `==` under reference semantics needs per-element pointer deref that blows up pointer analysis (depth-2 already TIMEOUTs; corpus needs 3-deep `==`), and the A/B sweep showed regressions (PASS 2710 vs by-value 2715 incl. a crash). The value-vs-reference tradeoff is fundamental in BMC; by-value + sound guards stays the default. Dicts/sets remain guarded. | [ref-semantics spike §10–§12](python-frontend-reference-semantics-spike.md) + [plan §0](python-frontend-plan.md#false-proofs) + [dict-byref](python-frontend-dict-value-byref-plan.md) |
| dict-literal const-fold | the dict-subscript const-fold substituted a construction-time-tracked value at a later read; for a value embedding a **mutable** symbol (a reassignable variable, a boxed-leaf pointer) this re-read the current value → false proof (`n=1; d={"k":n}; n=2; d["k"]`). **Closed 2026-06-24** (`value_is_const_foldable`): the const-fold now fires only for invariant values | sound (closed) | — |
| Native `smt_string`→int cast | under `--python-smt-strings`, a spurious str→int coercion from value plumbing lowers to `str.to_int` (defined-but-approximate); genuine `int(str)` is exact | sound (approximate, flagged) | [strings plan](python-frontend-strings-plan.md#strings) |
| BMC bounds | bugs deeper than `--unwind` / beyond the bounded container or 64-bit ranges are not found | sound w.r.t. the bound (intrinsic to BMC) | — (intrinsic) |

### B. Imprecisions (sound; spurious failures / nondet over-approximation)

| Area | Gap / deviation | Plan |
|---|---|---|
| Generators | list-with-cursor model: inter-yield side-effect ordering not faithful; module-global free vars in a generator `if` drop the body; cross-boundary list-shape | [plan §1](python-frontend-plan.md#generators) |
| Closures | **escaping** closures over-approximate captured free vars to nondet (late binding `lambda: i` in a loop imprecise); non-escaping + `nonlocal` mutation + capture-through-param are correct | [plan §2](python-frontend-plan.md#closures) + [fat-closure deep-dive](python-frontend-fat-closure-plan.md) |
| Strings (refined default) | ordering, substring `replace`, `split`, `casefold`/`title`, symbolic `count` — sound-but-imprecise; all precise (or precise-able) on the **native** backend opt-in | [strings plan](python-frontend-strings-plan.md#strings) |
| Regex | symbolic-subject and negated-membership (`re4`/`re11`) imprecise/slow on refined; precise on native | [strings plan §4](python-frontend-strings-plan.md#regex) |
| Lists | symbolic-list precision cluster (`nondet_list*`, `list_extend*`, `list-sort*`) — spurious failures; `list.count(x) == N` over a concrete list is not proven (precision) | [plan §9](python-frontend-plan.md#precision) |
| Tuples | fixed-tuple **slicing** `t[1:]` is now modelled exactly for constant bounds (step 1, negatives, `[::-1]`; `tuple-slice` CORE) — **was** mis-modelled (wrong `len`/index). Remaining: SYMBOLIC-bound or general-step tuple slices fall through (over-approx); a `tuple[int, ...]` variable-length sequence model (vs Any) is unbuilt — a precision item. `tuple.count`/`index` exact-value asserts not always proven | [plan §9](python-frontend-plan.md#precision) |
| Complex | `complex_*` edge precision (binop promotion, builtins, conjugate, `cmath` edges) | [plan §9](python-frontend-plan.md#precision) |
| Comprehensions | dict-comprehension over a runtime iterable (nondet); iteration-var scope leak; inner-iterator shadow | [plan §14](python-frontend-plan.md#residuals) |
| Descriptors / dynamic attrs | method shadowing, **data descriptors** (`__set__` + stateful `__get__`), **`@property` getters AND setters**, **`del self.x` + `__getattr__`-retype fallback** (2026-06-29: a deleted field in a `__getattr__`-class is `python_value` and `del` stores the fallback`s result, so the cross-type use is caught), and forward-referenced field access via a generic param are now modelled; **remaining**: truly-dynamic attribute *names* (`setattr(o, computed, v)`) need a runtime instance-`__dict__`, and `del o.x` through an Any-boxed function parameter does not propagate the deletion (the structural-mutation-through-a-call family, c2) | [plan §10](python-frontend-plan.md#descriptors) |
| Contracts | multi-level Liskov; strict-C3 mixin precedence; async | [plan §11](python-frontend-plan.md#icontract) |
| Higher-order | **bound methods as runtime values** (container-/conditional-/return-flowed, `m=obj.f`) now work (2026-06-22); container-/attribute-stored & composed *closures* remain (capture-through-param works); **~0 corpus value** | [plan §12](python-frontend-plan.md#higher-order) + [fat-closure](python-frontend-fat-closure-plan.md) |
| dict / cross-module | global-dict-literal mutation across modules is fixed; **`**d` unpack of a *mutated* global dict** and **non-dict cross-module globals** remain (rare) | [plan §5](python-frontend-plan.md#dict-byref) |
| dict symbolic-key / value-mutation cluster | **Characterized 2026-06-26 as MULTI-root, not one fix.** (a) symbolic-key *build* then constant-key *read* and missing-key KeyError detection already work (`d[k]=v` in a `.items()` loop, read `d["const"]`); (b) **FIXED**: an int/bool-keyed *constant re-store* read a stale value (`d={1:10}; d[1]=20; d[1]` folded to 10 — the dict_literals const-fold key-array is string-keyed), now drops the const-fold for that dict so the read uses the updated runtime array (`dict-int-key-restore`); (c) **OPEN (dict-value-by-reference)**: a dict-VALUE list/dict mutated in place (`d[k]=[]; d[k].append(x)`) does not propagate — the subscript-read value is a copy (only the int-literal value happens to alias via the lvalue slot); (d) **OPEN (symbolic-key over-approx)**: a genuinely symbolic/nondet key read over-approximates → spurious `python-model-bound` / `KeyError`. (c)+(d) are deep (per-instance value identity / symbolic dict modelling), not point fixes. **(e) CLOSED 2026-06-29 (`get`/`pop`/`setdefault` default type):** the `default` was coerced to the dict value type, losing its real type (`{}.get("k","s")` → int) — now the result is `value_type \| type(default)` (absent key ⇒ default in its own type; setdefault widens the empty-dict value type via inference). Moved to the soundness table (closes ty-005) | [plan §9](python-frontend-plan.md#precision) + [dict-value-byref](python-frontend-dict-value-byref-plan.md) |
| dict (untyped nested, unbounded ints) | values read out of an *untyped* nested dict iterated symbolically are over-approximated to nondet (sum-bound unprovable); orthogonal to leaf boxing | [plan §9](python-frontend-plan.md#precision) |
| Modules | `cmath`, fuller `os`/`time`/`datetime`/`json`/`dataclasses`/`collections` not modelled (nondet) | [plan §6](python-frontend-plan.md#modules) |
| Annotation checks (`--python-check-annotations`) | Opt-in, not default-on. The earlier two CBMC-core crash blockers are **resolved** (2026-06-26) and the checker-bug false positives are minimized (provenance-gating; unknown⇒Any). **Coverage extended (2026-06-29)** to the container-element boundary (`xs.append(v)` incl. a call arg `xs.append(src())`, via the callee`s static return type) and the dict-value-store boundary (`d[k]=v`); both flag-gated. **Precision improved (2026-06-29):** an Any-like (`python_value`) union component now satisfies the union (a list IS a `Sequence[str]`), and the element/dict-store checks are provenance-gated on EXPLICIT container annotations (not inferred `[]`/`{}`). It stays opt-in for a *semantic* reason: **default-on was MEASURED (2026-06-29) at ~1.25% spurious failures** (34/2718 sweep regressions, dominated by the irreducible class — real annotation mismatches the flag is designed to catch that are not runtime errors, e.g. `x: int = b.f()` where `f()->str` but the value is used as str) **and DECLINED**; default-on needs **use-site misuse gating**. Shipped as the `--python-strict` preset. Remaining checker FP: Any-valued dict (`check-annotations-any-dict-knownbug`) | [plan §7](python-frontend-plan.md#check-annotations) |

### C. Performance

| Area | Issue | Plan |
|---|---|---|
| `python_value` | field-by-field SSA expansion cost for symex-bound benchmarks | [plan §8](python-frontend-plan.md#performance) |
| Signature axioms | kwarg-check axiom volume | [plan §8](python-frontend-plan.md#performance) |
| Core hot path | `irept::operator==`; the TIMEOUT corpus tests | [plan §8](python-frontend-plan.md#performance) + [perf deep-dive](architectural/python-perf-analysis.md) |
| Default-backend string refinement | the nested-container TIMEOUTs (`github_3683`, `redundancy`, …) are dominated by the **default refined-string** refinement loop, not the shared container struct — the **native** SMT-String backend dispatches them ~15× faster and correctly (`github_3683`: 9 s vs. timeout). The `github_3684`-class **native crash** that previously blocked recommending native for nested string/int dicts is **fixed** by [leaf boxing](#leaf-boxing-non-fixed-width-values-in-byte-imaged-aggregates) | [strings plan](python-frontend-strings-plan.md#strings) |

### D. Intrinsic / by-design (not bugs)

| Numbers | default 64-bit `int` (`--python-unbounded-ints` opt-in) | — |
| Containers | bounded list/dict/set capacity | — |
| Identity | `is` + small-int interning approximated; `id()` deterministic | — (warned) |
| Async | `async`/`await`/async generators not modelled | [plan §13](python-frontend-plan.md#async) |

### E. NO CURRENT PLAN (explicitly flagged)

- **Call-signature error-message formatting** — the frontend now soundly
  detects missing/duplicate/unknown args (emitting a generic uncaught
  `TypeError`), but does **not** reproduce ESBMC's exact
  `TypeError: foo() missing …` / `Properties: N verified` strings, so
  those `github_30xx`/property-count sweep tests stay DIFF rather than
  PASS. No plan (cosmetic output-format alignment, low value).
- **`*args` + required keyword-only** signature combinations — the
  vararg guard skips the kwonly-required check. No plan (rare).
- **Native `casefold`/`title`** (Unicode case-mapping has no SMT-LIB
  primitive) and **symbolic `count`** — listed in the strings plan as
  residuals with no concrete encoding yet.
- **Cross-module `**d`-of-mutated-dict** and **non-dict cross-module
  global mutation** — no plan (rare).

## Where to make changes

| Want to add... | Touch | Why |
|---|---|---|
| New builtin (like `int`, `len`, `enumerate`) | `python_converter_call.cpp` `convert_call` | One file, one switch on `func_name` |
| New AST node type | `python_converter_*.cpp` corresponding to its category (statement vs expression) | The `convert_statement`, `convert_expression`, `convert_compare` etc. dispatch on `_type` strings from the AST JSON |
| New constant-tracking map | `python_converter.h` (declaration) + `convert_assign` (population) + invalidation hooks (`invalidate_loop_writes`, alias-targets, etc.) | The map needs both ends to stay sound |
| New library stub (e.g. `random`, `datetime`, `collections`) | `src/python/library/<name>.py` | Pure Python; the converter ingests it just like user code |
| New language flag | `python_language.{h,cpp}` (declare + parse) + `set_python_*` setter on `python_convertert` | The flag needs to be threaded from CLI to the converter |
| New checker property | `add_check(cond, kind, message, loc)` from any converter file | Properties register with the frontend's `pending_checks` and surface in CBMC output |
| New boundary call site (call / assign / return) | Use `coerce_call_argument`, `coerce_assign_rhs`, or `coerce_return_value` instead of `safe_typecast` | The boundary helpers apply PLR §3.2 None-marker adaptations; raw `safe_typecast` would emit NULL-deref for typed-None — see the "Type-coercion at boundaries" section above |
| New class-constructor call site | Call `build_class_init_call(class_name, self_lvalue, call_ast, loc)` and wrap the result in `code_expressiont` | Centralises MRO walk + arg conversion + kwarg matching + default padding + boundary coercion in one place |
| New PLR adaptation for typed slots (e.g. `Optional[list]` marker) | `coerce_to_typed_slot` in `python_converter.cpp` | All four boundary helpers (call/assign/return/element) delegate here, so the rule applies everywhere uniformly |

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
for full flag reference and tuning advice. For mypy-style static
type-annotation enforcement, add the opt-in `--python-strict` preset
(`--python-check-annotations` + `--python-missing-return-check` +
`--python-required-kwarg-checks` + `--python-check-typeddict-fields`).
