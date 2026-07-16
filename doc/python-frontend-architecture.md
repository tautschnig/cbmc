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
or downgraded. The main invalidation drivers:

- **`invalidate_reassigned_symbol(sym)`** in `python_converter.cpp` — the
  canonical "a Name is being (re)assigned" invalidation. Drops cached
  list/tuple/dict literals that REFERENCE `sym` (stale-symbol-in-container) and
  clears `sym`'s own scalar constants (`float_constants`/`string_constants`); a
  site that rebinds `sym` to a constant/literal re-establishes precise tracking
  afterwards. **Every** reassignment site MUST call it: tuple-unpack targets,
  walrus (`:=`), the try-block-split assign path, finally- AND try-`else`-assigned
  names (the `clear_assigned` scan in `convert_try` covers both clauses after the
  arm-merge), the
  `for`/`with as` targets, augmented assign (referencing-literal half), and
  match-statement capture patterns (invalidated AFTER the per-arm
  `restore_tracking`, since the match snapshots/restores tracking around each
  arm). This consolidates a rule that was previously replicated per-site and
  repeatedly missed — a recurring false-proof class where a rebind
  (`b = 1; a, b = (t[0], t[1]); t[b]`, or via `for`/`with`/walrus/finally/aug/
  `match ... case .. as b`) left a stale constant and a later `t[b]` folded the
  index to the old value (wrong element + masked IndexError). A whole-codebase
  audit of every reassignment construct confirmed all are now sound (no false
  proofs; residual starred-capture length imprecision is a sound over-approx).

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

For **lists**, the inference is a JOIN over all `append`/`extend` sites with
**Any-dominance** (2026-06-30, soundness): if an appended element's type is
**uninferable** (e.g. a call result the prescan can't resolve) OR two appends
have **different** concrete types (a heterogeneous list), the element type
becomes `python_value` (Any), which *dominates* any prior concrete inference.
This is sound: an unknown/heterogeneous element really is Any — defaulting it to a
concrete `int` would PUN a non-int store (cast to int) and let a later
`isinstance(xs[0], int)` fold to true (a false proof; `list-element-infer-any`,
CORE). The common, well-typed case (literal appends, annotated-return calls)
stays concrete (no perf cost — measured 0 sweep regressions). NB: an EXPLICIT
`list[int]` annotation still types the element `int` and so still puns a
mismatched store — that is the intrinsic *annotation-laundering* residual
(`slot-pun-list-element-knownbug`), not this inference path.

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
   the populated list at the end. The append is built by
   `build_gen_result_append` and emitted from **both** the
   bare-statement path (`convert_expr_stmt`, for `yield X;`)
   **and** `convert_expression` (for an *expression-context*
   yield such as `x = yield 1`, `f(yield 1)`). The statement
   path returns early, so each yield is counted **exactly
   once**. (Counting expression-context yields was a 2026-06-30
   fix; previously `x = yield 1` was dropped and such
   generators under-counted their yields.) The *value* of a
   yield expression — the value sent in via `.send()` — is not
   tracked by the eager model (plan §1 Phase 2).
3. A call site `g = gen()` triggers
   `allocate_generator_cursor` in `convert_assign`, which
   allocates `__cursor_g` (int64, initialised to 0) and
   records the pairing in `generator_cursors[g] = cursor_g`.
   The cursor doubles as the **priming state**: because
   `next()` does `cursor++` *before* returning, `cursor == 0`
   is exactly the "suspended before the first line" state (no
   separate `__started` flag is kept — single source of truth).
4. `next(g)` in `convert_call`:
   - Checks `if cursor >= length: raise StopIteration` via
     `pending_checks` setting `__exception_active = true`
     and `__exception_type = hash("StopIteration")`.
   - Otherwise increments `cursor`.
   - Returns `data[max(cursor - 1, 0)]`.
5. `g.send(v)` (handled as a list method, since the generator
   object *is* the eager list) resolves `g`'s cursor and, per
   PEP 342, raises `TypeError` when the generator is
   **just-started and the sent value is non-None**
   (`emit_conditional_exception(cursor == 0 ∧ v≠None, …)`),
   then resumes like `next()`. *Soundness gating (no false
   positive):* a provably-`None` send is never flagged; a
   concrete-typed `v` is never `None`; a `python_value` `v` is
   guarded on its `NONE` tag; an opaque/aliased generator with
   no resolvable cursor is not flagged. Faithful value-passing
   into the pending `yield` needs real resumption (plan §1
   Phase 2).
6. `for x in g` **resumes from `g`'s cursor** (2026-07-01): the
   loop counter is initialised to the generator's cursor (0 for
   a fresh generator) and the cursor is set to the length
   afterwards, so `next(g); for x in g` yields the remaining
   elements and a subsequent `next(g)`/`for` sees exhaustion.
   (A generator called inline — `for x in g()` — has a fresh
   cursor of 0, unchanged.)

Existing exception infrastructure handles the StopIteration
propagation; `try / except StopIteration:` catches it
without further changes.

**KNOWN false-proof cluster — generator consumption-state /
identity (partially UNSOUND).** The cursor tracks consumption
correctly for a *direct* `next(name)` / `name.send(v)` and now
for a **`for` loop** (above). The other consumption channels are
also handled now — the consumption-state cluster is CLOSED:
- **CLOSED (2026-07-01):** `for x in g` after a partial `next(g)`
  now resumes from the cursor (`gen-foriter-after-next-typeerror`,
  CORE).
- **CLOSED (2026-07-01):** an alias `it2 = it` now shares the
  consumption cursor (`gen-alias-consume-typeerror`, CORE) — the
  alias-assign path propagates `generator_cursors[it2]`, and the
  pointer-alias's reads auto-dereference to the list struct so
  the cursor path resolves.
- **CLOSED (2026-07-02):** `list(g)` / `sum(g)` after a partial
  `next(g)` are cursor-aware (`gen-aggregating-after-next`, CORE).
- **CLOSED (2026-07-06, `1f6b9d3905`):** a generator in a container
  consumed via the slot (`box = [g()]; next(box[0])`) has no
  per-Name cursor, so `next()` on a STORED channel (a Subscript /
  Attribute receiver) returns a sound NONDET yield instead of the
  unsound first-yield guess — it can no longer prove a stale value
  (`gen-in-container-consume`, CORE). Precise fresh (`next(g())`) and
  Name-cursor consumption are unaffected.
Passing a generator to a function is now SOUND (2026-07-10): the
callee consumes an independent (by-value) cursor, so the caller's
cursor was left unadvanced and a subsequent `next(it)` re-yielded
from the start — a false proof (`it=g(); c(it); next(it)==1` was
proved). Found by the generator-identity spike; the exact channel
the earlier draft wrongly claimed was already sound. Fixed by
soundly HAVOC-ing the caller's cursor to a nondet position in
`[0, length]` after a generator Name is passed to a user function
(a callee may advance it by an unknown amount), so a re-yield can
no longer be proved (`gen-func-consume-sound`, CORE). A callee that
does not consume loses precision (sound over-approximation); a
precise shared cursor does not fit the once-converted-callee model.
A fully PRECISE generator-OBJECT model (consumption state tied to
the object and shared through container/attribute/param channels)
remains future work (plan §1) — but every channel is now SOUND.
(These were found by a proactive soundness sweep and the
mutation-oracle, not the oracle corpus.)

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

## Decorator application (PLR §8.7)

`@dec def f` lowers to `f = dec(f)` at def-time. Special decorators
(`@overload`/`@staticmethod`/`@classmethod`/`@property`/`@c_intrinsic`, and the
`@icontract.*` family) are recognised and handled by their own machinery; the
rest are processed by the **`user_decorators` loop** in `convert_function_def`,
which applies them bottom-up and registers a `function_aliases` entry so calls to
`f` dispatch through the decorator's returned wrapper.

- **Def-time callability (`dec_not_callable`).** A bare `@name` decorator whose
  value is *provably non-callable* raises `TypeError` at the def site. "Provably
  non-callable" = the resolved value is neither `ID_code` (a function / method /
  lambda / class) nor a `python_value` (Any), and — if it is a user-class
  instance — its MRO does **not** define `__call__` (`concrete_class_lacks_dunder`).
  This is emitted at **two** points because a module-level `def` registers in an
  earlier pass and contributes *no* runtime code to `__main__`: (a) module-level
  defs emit the check in `convert_module_body`'s FunctionDef branch, with an
  explicit `uncaught exception` assert (the per-statement loop skips FunctionDef);
  (b) nested defs emit into the `dec_block` that `convert_function_def` returns
  (those reach the enclosing body via `convert_statement`). *No-FP gating:* only a
  bare `@Name` is checked — a Call (`@factory(...)`) or Attribute (`@mod.deco`,
  e.g. `@icontract.require`, `@functools.wraps`) form is a factory / library
  decorator whose callability cannot be proven, so it is never flagged; a `nil`
  (unresolved) or `python_value` (Any) decorator is never flagged.
- **Wrapper-arity (`dec_wrong_arity`).** The wrapper symbol is looked up by its
  **qualified** name (`python::<dec>::<inner>`, falling back to the bare name) so
  the alias points at the real nested wrapper; `validate_call_signature` then
  enforces the *wrapper's* arity at the call site (the wrapper is undecorated, so
  it is in `function_signature_checkable`). A guard prevents the decorator's
  fn-parameter binding from clobbering the decorated-function→wrapper alias when
  the parameter shares the decorated function's name (`def dec(f): … @dec def f`),
  which would otherwise mask the wrong-arity call.

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
`attr`. PLR §3.3.5 isinstance-narrowing is respected. PLR §3.3.2: a plain
`param.attr = ...` STORE *creates* the attribute, so it is treated as a creation
(not a missing-attribute access) and is excluded from the flagged read-uses —
including a later read of the created attr (`o.x = 5; return o.x`); a Load read,
an `AugAssign` target, and a nested target read (`o.x[i]=`) are still flagged
(`any-arg-attr-store-nofp` CORE). Default-on for `.py`. (Residual precision: the
Any-param store→read VALUE does not propagate — `f(o)` that sets `o.x=5` then
reads it returns nondet, not 5 — a separate item.)

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
| List element, **inferred** (`xs = []; xs.append(u)`) | empty-list element-type inference defaults uninferable/heterogeneous elements to `python_value` (Any-dominance) → tag preserved | **sound** (2026-06-30; `list-element-infer-any` CORE) |
| List element — any store form (`append`/`extend`/`insert`, subscript-store `xs[0]=u`), empty- OR non-empty-init, any annotation form | the element-inference pre-pass WIDENS the element to `python_value` (Any-dominance) when a store's type is uninferable or incompatible with the declared `T` → tag preserved, use-site obligation fires; a correctly-typed store keeps `T` (precision) | **sound** (2026-07-10; `slot-pun-list-element-{append,insert,subscript}-sound`(+`-nofp`) CORE). Under `--python-check-annotations` the concrete `T` is kept so the MISMATCH is reported as a property (`check-annotations-list-append`). Slice-assignment `xs[i:j]=[..]` is excluded from the scan (its RHS is a list of elements, not one element) |
| **Attribute field**, annotated — INIT store (`self.x: int = v`, v uninferable/mismatched) | the field-typing scan WIDENS the field to `python_value` when the init store is uninferable (unannotated param → Any) or a different scalar category → tag preserved; also fixes later external stores to the widened field. A correctly-typed init keeps the concrete type | **sound** (2026-07-10; `slot-pun-attr-field-init-sound`(+`-nofp`) CORE) |
| **Attribute field**, annotated — EXTERNAL store to a CORRECTLY-init'd field (`self.x: int = 0; o.x = u`) | a module-wide scan (keyed on the attribute NAME, a sound over-approximation like the del + __getattr__ scan) records the scalar CATEGORY of each external `<expr>.attr = value` store; a concrete scalar field whose category differs is WIDENED to python_value at class-definition time → tag preserved. Store category resolved from the AST (Constant / callee `-> T` / first `return <Constant>`); a bare Name RHS is left alone (no over-widening) | **sound** (2026-07-10; `slot-pun-attr-field-external-sound`(+`-nofp`) CORE). Under `--python-check-annotations` the concrete type is kept so the mismatch is reported as a property. **This closes the LAST slot-pun — the concrete-slot-punning whole-group is fully closed.** (Untyped field is `python_value` → sound; the tagged-union field `x: int|str` is caught by a tag obligation, `tagged-union-narrowing-unsound` CORE) |

**The architectural invariant the audit reveals:** a slot typed `python_value`
(Any) *preserves* the runtime tag, so a later misuse is caught by the
operator/subscript/call tag obligations — it is sound. A slot with a *concrete*
type (`list[int]`, `field: int`) drops the tag on store, so a wrong-tagged value
is read back at the concrete type and a misuse does not fault. **The
`python_value`-preserving default is now applied wherever the slot type is
INFERRED rather than annotated:** unannotated dict values, and (2026-06-30)
empty-list elements that are uninferable/heterogeneous — both stay `python_value`,
so untyped containers are sound. The residual is the **annotated** rows
(`list[int]`, `field: int`): here the explicit annotation forces a concrete slot,
and the principled fix would be **slot-widening** (type the element / field
`python_value` when a `python_value` is stored, the same move the return slot
makes). That is **invasive + perf-costly** (it changes container/struct element
typing, with the precision/perf cost that drove the concrete-typing design and
the not-viable `--python-ref-mutables` default). **The concrete-slot-punning
whole-group is now FULLY CLOSED (2026-07-10)** — no residual slot-pun remains
(the previously-deferred attribute-field external store is closed below).
**Progress (2026-07-10):** the **entire list-element** punning class is now
**closed** — every store form (`append`/`extend`/`insert`, subscript-store
`xs[i]=u`), empty- OR non-empty-init, any annotation form, widens the element to
`python_value` (Any-dominance) at the list's creation site when a store is
uninferable or incompatible with the declared `T`, preserving the tag; a
correctly-typed store keeps `T` (`slot-pun-list-element-{append,insert,subscript}-sound`(+`-nofp`)
CORE). This closed a real, previously-unpinned false proof (unquoted
`list[int].append(src())` punned a str into int; only the quoted-forward-ref
variant was accidentally sound before) plus the subscript-store/non-empty-init
paths. The element-inference pre-pass is the right architecture (element typing
is fixed at the list's creation site; the widening is targeted so it avoids the
global `python_value` perf cliff) and slice-assignment `xs[i:j]=[..]` is excluded
(its RHS is a list of elements, not one element). The **attribute field** class
is now *mostly* closed the same way: a `self.x: T = v` INIT store whose value is
uninferable (unannotated param → Any) or a mismatched scalar widens the field to
`python_value` at the field-typing scan (`slot-pun-attr-field-init-sound`(+`-nofp`)
CORE), which also fixes later external stores to that widened field. The EXTERNAL
store to a CORRECTLY-initialised field (`self.x: int = 0; o.x = u`) is **now also
closed (2026-07-10)**: a module-wide scan (keyed on the attribute NAME, like the
del + __getattr__ scan) records the scalar category of each external
`<expr>.attr = value` store — resolved from the AST (Constant / callee `-> T` /
first `return <Constant>`) so it is order-independent — and widens a concrete
scalar field whose category differs (`slot-pun-attr-field-external-sound`(+`-nofp`)
CORE). **No slot-pun residual remains.** (Under `--python-check-annotations` the
concrete `T` is kept and the mismatch is reported as a property.)
Two *adjacent* cases that the earlier draft lumped here are now **CLOSED**:
composition/object-identity aliasing (`shared-object-aliasing`, CORE — fixed by
reference semantics, a distinct root) and the **tagged-union** field case
(`x: int | str`; `tagged-union-narrowing-unsound`, CORE — fixed by a tag
obligation on union-field extraction). Tag obligations are NOT a default-mode fix
for the annotated rows:
storing a mismatched value is legal Python (the error arises on a later
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

**CURRENT STATE (2026-07-16) — read this first; the dated notes below are a
chronological changelog.** The differential oracle (external CPython-semantics
corpus, default config) tracks **0 known false proofs**, plus **3 intrinsic /
out-of-subset residuals** (the annotation-laundering pair `004` call-arg +
`ty-010` return-annotation — flag-gated under `--python-check-annotations` — and
`d1` int→float, out of subset; the former third member `007` list-element is now
**CLOSED** by the slot-pun widening, see the 2026-07-10/13 note) and ~200 false
*alarms* (sound over-approximations / unsupported-feature precision — see
inventory B). **Every fuzzing axis is at zero false proofs**: the two standing
gates (narrow PLR-fuzz + negated mutation-oracle), the WIDE negated sweep, AND
the WIDE value sweep (3000 seeds each), alongside the oracle. **No deferred
soundness residual remains**. The 2026-07-14/16 real-world campaign (see that
changelog note) additionally closed the **UNSAT-vacuity GLOBAL false-proof
class** (string-emitter output symbols shared across re-executions poisoned the
whole formula — six vectors fixed, one historical sweep PASS proven vacuous and
re-baselined), fixed a symex CRASH class, made TypeError/AttributeError
obligations handler-aware per PLR §8.4, and added PEP 649 version-dependent
annotation semantics. Two standing soundness-
regression gates run after every change: the **oracle 0-NEW gate** (real-world
corpus) and the **PLR-fuzz 0-NEW gate** (template + randomized PLR-tagged programs
vs a committed baseline of **0** false-proof labels — see Sweep rounds 4–8). **The
fuzzer and oracle are now BOTH at zero false proofs** (the soundness arc drove the
debt 73 → 0); the fuzzer's remaining output is precision-only FALSE_ALARMs (sound).
Both architectural roots that held the last residuals are now FULLY CLOSED, each
via spike-found separable paths rather than the multi-day infrastructure they were
assumed to need. **(A) Reference-identity (object identity):** `del`-of-closure-
captured-var (`be037ec01a`, deleted-flag guard at the capture-read — no cell-
capture), `del c.a` attribute-read (`41d6788612`, per-instance `__present_<attr>`
flag — independent of Phase-4 since instances are already by-reference), and
generator-in-a-container consumed via the slot (`1f6b9d3905`, sound nondet for
`next()` on a stored Subscript/Attribute channel, keeping fresh/Name cursors
precise). **(B) List-length/identity:** mixed-concat (`2eceff5929`/`c9d592fed8`,
constant-fold + element-widening retype) and `min`/`max`-empty via a
`range(non-constant bound)` comprehension (`e766dde276`, symbolic-length range-
comprehension: exact length with no filter, `[0,bound]` with a filter). (The 10
list-bitwise labels, the `list(g)`/`sum(g)`-after-`next()` aggregating channels,
the plain-class `missing_attr` read, `gen.close()`+next, `del x` DIRECT-read
NameError, and the min/max/sorted mixed-category-via-concat that were baselined
earlier are now all **closed** — see Sweep rounds 4–7.)

> **Mutation-oracle + value-computation soundness campaign (2026-07-07).** The
> value-oracle fuzzer (`assert r == V`, V = CPython's value) has a structural
> blind spot: when cbmc MIScomputes a value, `r == V` merely FAILS (a false
> *alarm*), never a false *proof* — so value-miscomputation soundness bugs hide
> behind false alarms. Added a **negated value-oracle / mutation-oracle** mode
> (`PLR_NEGATE_ORACLE=1`): it emits `assert r != V`, so CPython always raises and
> cbmc verifying SUCCESSFUL pinpoints a DEFINITE value miscomputation. A
> nondet/imprecise `r` cannot be proven `!= V`, so the mode discriminates
> soundness from precision. It runs as a **second standing gate pass**
> (`baseline-negate.json`, empty). Triaging value-oracle false alarms + running
> the mutation-oracle over a progressively widened grammar (containers, slicing
> incl. step/negative, comprehensions, sets, string methods, f-strings,
> try/except, `.index`/`reversed`) drove a whole batch of value-computation
> soundness fixes to **0**: in-place-list-mutation stale constant-fold
> (`8d0dbf3d0f`/`e1e7e6cfc9`, whole-group across methods + subscript/slice/aug
> stores), self-referential aggregate reassignment read-write hazard
> (`3ad447fb84`, evaluate-then-bind), empty-slice negative length (`4cae112617`),
> unsupported step slice (`a0922d4aed`, sound over-approx), `reversed(list)`
> returning the list unchanged (`6b522077f0`), symbolic `tuple.index`
> (`c91476a4b4`), and the **cross-element-type list `==`/`!=` whole-group**
> (`ef71196c6f` double-negation in the bridge tunnel + `40d5b7d91d` bool-element
> bridging). Both gate passes are green across the widened grammar; convergence
> is strong (each widening pass now yields ≤1 bug, all in the comparison group).
>
> **Continuation (2026-07-07/08).** Two further soundness bugs surfaced by the
> widened mutation-oracle + precision triage, both fixed as whole-groups:
> **first-raised-exception-wins** (`d6bd7b25ed`, PLR §8.3) — `emit_conditional_
> exception` overwrote a *pending* exception, so an arg-evaluation exception
> inside `try/except` could be wrongly caught by the outer handler; now every
> conditional exception is guarded by `not __exception_active` so the first raise
> wins. **Stale-symbol-in-`list_literals`** (`3803c3fa7e`, PLR §3.1) — a cached
> list struct referencing a live symbol (`xs = xs + [b]`) went stale when the
> symbol was reassigned (`b = 9`); a later fold/read reusing the cached struct
> read the NEW value → false proof (pre-existing, also affected the concat fold).
> Fixed by INVALIDATING any cached list referencing a symbol when that symbol is
> reassigned (keeps symbol-bearing caches, so closure/comprehension precision
> tests still pass — the first, blunter "don't cache symbol-bearing structs" fix
> was rejected because it regressed two such tests). Precision in the same arc:
> constant-fold-through-operations now spans slice (`67e5f2fc70` step-1/reverse,
> `18b48b949e`/`c4e1ed5b14` constant-step via Python `slice.indices`) and repeat
> (`xs*n`, bundled in `3803c3fa7e`) over a constant list, plus `list.count()`
> modelled as a symbolic occurrence count (`82e55fba06`). Value-oracle false
> alarms fell 87 → 67 across the arc; the negated (mutation) oracle sweep is at
> **0 miscomputations** and both standing gate passes stay green. CORE guards:
> `list-literal-stale-symbol`(+`-nofp`), `list-count`, `slice-constant-fold-chain`.
>
> **Continuation (2026-07-08) — tuples & list-of-tuples.** Widening the mutation-
> oracle grammar (behind `PLR_WIDE`, default OFF to keep the standing gate green)
> with tuple folds, `list(zip)`/`list(enumerate)`/`map`, and rebuild-container-
> from-variables surfaced a batch (17 negated FPs) fixed as whole-groups:
> **(1) list `==` compared the whole fixed-size backing array, not just the first
> `length` elements** (`6e6ef86280`, PLR §6.10.1) — `list(enumerate(xs))` leaves
> OOB reads beyond length while a literal pads with zeros, so `!=` was wrongly
> PROVED; now a length-bounded element-wise compare (string by content, float via
> ieee). **(2) tuple mixed with a non-tuple in arithmetic** (`b20aa67663`, PLR
> §6.3) — had no incompatibility case, so `int + tuple` (incl. the mistyped
> `tuple()` nondet) reached the arithmetic builder and CRASHED ("add/sub with
> mixed types" invariant); now a TypeError. **(3) non-constant different-element-
> type list concat** (`388d8c9b01`, PLR §6.3.2) — the general concat read both
> operands with the LEFT element type, mis-typing e.g. `list(zip(..)) + [5]` into
> a definite-wrong value (false-proved `!=`); now a sound nondet list. Precision:
> constant tuple concat folds (`56f73dfd14`). Wide-negated FPs 17 → 4.
> **Architectural residual (soundness, tracked):** `python_value` has **no TUPLE
> tag**, so a tuple cannot be soundly boxed. Lists whose elements are tuples,
> flowing through the heterogeneous/box-requiring operations (set/sorted/slice in
> certain combinations), remain a residual FALSE-PROOF class exercised only under
> `PLR_WIDE` (the standing narrow gate is at 0). The whole-group fix is a
> `python_value` TUPLE tag + tuple-aware boxing/`structural_eq`/materialisation —
> a representation change, not a point fix (design + phased plan:
> [python-frontend-tuple-tag-plan.md](python-frontend-tuple-tag-plan.md)). (`rand_2660`-style residuals are
> instead mutation-oracle unwinding artifacts: `range(sum(xs))` cannot fully
> unroll at the sweep bound, so `r != <full literal>` is legitimately true under
> bounded execution — a harness limitation, not a frontend bug.)
>
> **Continuation (2026-07-08) — heterogeneous elements, dict iteration &
> evaluate-once.** Further widening (nested references, dict value/key iteration,
> class instances) surfaced three more whole-group soundness fixes.
> **(1) list[python_value] element equality by pointer** (`54b6c3a71b`, PLR
> §6.10.1) — element comparison used a plain equal_exprt on the tagged union
> (incl. the heap pointer), so equal-but-distinct references (nested list/dict/
> set, or a tuple-as-CLASS) compared unequal → `!=` wrongly PROVED; now routed
> through `python_value_structural_eq` (scalars precise, strings by content,
> pointer-backed tags → sound nondet). **(2) dict variable-key store left the
> `dict_literals` snapshot stale** (`f7d5f10977`, PLR §6.2.7) — `d[b]=v` (non-
> constant key) updated the runtime arrays but not the constant snapshot, so
> `d.values()`/`keys()`/`items()` read the pre-insert state (false proof); now the
> snapshot is dropped on a variable-key store (same whole-group as the list
> stale-snapshot invalidation). **(3) evaluate-once for operand-duplicating
> builtins** (`b116b0d516`/`edfbebebb4` abs, `00ddd0845b` min/max, PLR §6.2/§6.10)
> — abs()/min()/max() duplicate their operand across dispatch/comparison branches
> (min/max additionally re-converted the 2-arg case), so a side-effecting call arg
> (`abs(o.bump())`, a mutating method) executed multiple times and read a stale
> value (false proof); operands are now materialised once via a shared
> `materialize_call_operand` helper (a broad materialise-at-the-Call-chokepoint
> variant was REJECTED — it regressed closure/generator call semantics, which
> consume the raw call expression). Wide-negated FPs 4 → 2 (the only remaining
> residual is the tuple-tag class above). CORE guards:
> `list-pv-element-ref-equality`, `dict-varkey-store-iteration`,
> `abs-side-effecting-arg`, `min-max-side-effecting-arg`.
>
> **Correction (2026-07-08 spike).** The last two `PLR_WIDE` negated residuals
> (attributed above to the TUPLE-tag class) were root-caused to a DIFFERENT and
> broader whole-group: **retype-on-reassign** -- reassigning a list variable to a
> value of a different element/container type (`xs=[6,2]` then `xs=["a","b"]` /
> `[7.0,8.0]` / `[(1,2)]` / `list(zip(xs,xs))`) reinterprets the bits instead of
> rebinding the name -> false proof. Scalars already rebind soundly. A single-site
> retype fix was insufficient (two paths: coerce_assign_rhs + the assign-cast) and
> was reverted. **Now CLOSED (`c295841ecf`):** both reassignment paths retype the
> binding for an aggregate RHS (Python rebinds), so a differently-typed list/dict/
> tuple/set reassignment is no longer reinterpreted. The **`PLR_WIDE` negated
> sweep is now 0** (all residuals closed); the standing gates + oracle stay at 0.
> Root cause + both paths: [python-frontend-retype-on-reassign-plan.md](python-frontend-retype-on-reassign-plan.md).
> The `python_value` TUPLE tag remains a separate PRECISION item (boxed-tuple
> isinstance/extract/Any are sound false alarms), tracked in its own plan.
>
> **Continuation (2026-07-09/10) — tuple methods, exception propagation, and the
> reassignment stale-tracking whole-group + consolidation + AUDIT.** Widening the
> mutation-oracle grammar further (behind `PLR_WIDE`: boolean short-circuit,
> walrus, chained assignment, plus direct probing) closed the following, each as a
> whole-group:
> - **`python_value` TUPLE tag P1** (`40815b36c5`, PLR §6.10) — isinstance
>   recognises boxed tuples; boxed-tuple truthiness is sound nondet (fixed a
>   latent empty-tuple false proof); `structural_eq` has a TUPLE case. Boxed-tuple
>   extract/Any remain a sound PRECISION residual (own plan).
> - **tuple `+`/`*` folds & mixed-arithmetic TypeError** (`56f73dfd14`,
>   `f626285c4a`, `b20aa67663`) and **`tuple(iterable)` over a constant list →
>   fixed-arity tuple** (`39c5f92644`).
> - **`tuple.index`/`count` family** (`e57f15b1d1` empty-tuple ValueError,
>   `abdd981786` symbolic numeric elements, `375693095b` sound may-raise fallback,
>   `9cc1ea1423` over the tuple TYPE components, `c91476a4b4` symbolic-absent
>   ValueError) — all now sound (may-raise where presence isn't provable).
> - **Augmented dict subscript `d[k]+=v` on a missing key → KeyError**
>   (`cf3e51150d`, PLR §6.2.7), restricted the defaultdict auto-insert skip to
>   actual defaultdicts. **`f(*xs)` non-literal spread → sound nondet `*args`**
>   (`10129615ca`, PLR §8.7).
> - **Exception-propagation whole-group** (theme: *an exception must propagate
>   through every evaluation site*): dict-subscript KeyError must not overwrite a
>   pending exception — first-raise-wins (`5fcecc9d79`, PLR §8.3, extends
>   `d6bd7b25ed`); `with EXPR as v` flushes the context-expression's exception
>   checks before the body (`a7d713850c`, PLR §8.5).
> - **Reassignment stale-tracking whole-group.** A rebind of a name must drop
>   cached list/tuple/dict literals REFERENCING it AND its own scalar constants,
>   else a later `t[b]` folds the index on the stale value (wrong element + masks
>   an IndexError — a false proof). This rule was replicated per-site and
>   repeatedly missed; the campaign fixed each site (`fd85bd9ea1`/`94967b0dc9`
>   tuple-unpack, `8663ee9b87` try-block-split, `16e58ddf48` finally-assigned,
>   `f1f48bf738` walrus, plus the earlier `3803c3fa7e`/`079b193810` list/tuple/
>   dict-literal generalisation) then **CONSOLIDATED** them into one
>   `invalidate_reassigned_symbol(sym)` helper (`65a47547ef`) — see the
>   [Invalidation](#invalidation) section. A whole-codebase **audit** of every
>   reassignment construct with the consolidated invariant then found two MORE
>   latent instances, fixed via the helper: the `for`/`with as` targets
>   (`65a47547ef`), augmented-assign's referencing-literal half (`3ae74cb72e`,
>   PLR §7.2.2), and match-statement capture patterns (`1bf48a0429`, PLR §11.6 —
>   subtle: the match handler snapshots/restores tracking per arm, so captures are
>   invalidated AFTER `restore_tracking`). The audit confirmed all reassignment
>   constructs (plain/ann/aug/unpack/for/with/walrus/except-as/global/del/chained/
>   match) are sound; residual FAs (starred-capture `a,*b=..` length,
>   match-sequence-star) are sound over-approximations, not false proofs. **The
>   reassignment-invalidation whole-group is CLOSED.** CORE guards:
>   `walrus-stale-tracking`, `walrus-const-tracking`, `for-target-stale-tracking`,
>   `with-as-stale-tracking`, `augassign-stale-container`,
>   `match-capture-stale-tracking` (plus the earlier `*-stale-tracking` guards).
>
> Both standing gate passes (narrow PLR-fuzz + negated mutation-oracle) stay at 0;
> the wide negated sweep is at 0; the oracle is at 0-NEW. The `PLR_WIDE` value
> residual (the `tuple()`-of-computed-list / `.index` and try-else stale-tracking
> cases) is now **CLOSED (2026-07-10)**: `tuple(<non-constant iterable>)` returns a
> nondet `python_value` and `.index` on an unresolved receiver soundly may-raises
> (`de482e6345`); and try-`else`-assigned names have their tracking cleared after
> the arm-merge like the finally clause (`5321a713c4`). **Both the WIDE negated
> AND WIDE value sweeps (3000 seeds each) are now at 0 false proofs.**
>
> **Exception-propagation audit (2026-07-10).** Systematically probed the invariant
> "a raising subexpression must propagate at EVERY evaluation site" (the theme
> behind first-exception-wins / with-context-expr) by embedding an always-raising
> `[][0]` at ~27 sites: call args/kwargs, nested calls, binop operands,
> compare/chained-compare, boolean short-circuit operands, conditional-expr
> arms, list/tuple/set/dict literal elements (and dict keys), f-strings,
> subscript index, return value, walrus, comprehension element AND iterable,
> aug value. **All propagate correctly** (each FAILED) — the frontend's exception
> propagation is comprehensive. The audit found ONE gap: **default argument
> values that raise are not evaluated at def-time** (`def f(a=[][0])` — Python
> evaluates defaults once when the `def` executes, so it raises there; the
> frontend only re-derives the default at call sites for its value, swallowing a
>> raising default). **CLOSED (2026-07-13, `ccc371931d`).** Def-time evaluation is
> now emitted at the def's SOURCE-ORDER position across all three paths where a
> def executes (module pass for top-level defs, ClassDef handling for methods,
> `convert_statement` for nested defs), plus the uncaught-exception assertion the
> general per-statement check omits for FunctionDef/ClassDef. Emitting in source
> order is exactly what dissolves the earlier spike's false alarm (`def f(a=G[1])`
> reading an already-bound global) — G's assignment is added to the module block
> before the def. CORE `default-arg-raises-sound`, `default-arg-nested-method-sound`,
> `default-arg-ordering-nofp`. **This was the last deferred soundness residual.**
>
> **Continuation (2026-07-10 pm → 2026-07-13) — slot-pun closure, generator
> func-consume, try-else tracking, def-time defaults; ZERO-residual milestone.**
> The arc that closed every remaining known false-proof class:
> - **Concrete-slot-punning whole-group FULLY CLOSED** (see the coercion-boundary
>   audit table): list element — append/extend/insert (`f56bda8fd9`, incl. the
>   previously-unpinned unquoted `list[int].append(src())` false proof),
>   subscript-store + non-empty-init (`217c916f56`, slice-assignment excluded);
>   attribute field — init store (`7bdbe6c01f`) and EXTERNAL store
>   (`60826f775f`, module-wide attribute-NAME-keyed scan mirroring the
>   del+__getattr__ precedent, AST-resolved store categories so definition order
>   is immaterial). All widenings are TARGETED (only a detected mismatched /
>   uninferable store widens the slot to `python_value`) — no measurable
>   precision or perf cost (sweep 2719/0 at every step); gated to default mode
>   (`--python-check-annotations` keeps the concrete type and reports the
>   mismatch as a property).
> - **Generator func-consume channel** (`8115f204c1`): passing a generator Name
>   to a user function now soundly HAVOCs the caller's cursor to `[0, length]`
>   (the callee consumes by-value, so the caller's cursor was left unadvanced —
>   `it=g(); c(it); next(it)==1` was a false proof the earlier draft wrongly
>   claimed sound). Every generator consumption channel is now sound.
> - **`tuple(<non-constant iterable>).index`** (`de482e6345`): tuple() over e.g.
>   `zip(...)` returns a nondet `python_value` (was a MIStyped python_int) and
>   `.index` on an unresolved receiver soundly may-raises.
> - **try-`else` stale tracking** (`5321a713c4`): else-assigned names now have
>   their tracking cleared after the arm-merge (the same reassignment whole-group
>   as `finally`; the `clear_assigned` scan is hoisted to cover both). With this
>   + the tuple.index fix, **the WIDE value sweep reached 0** (all 4 documented
>   residuals closed).
> - **Def-time default-argument evaluation** (`ccc371931d`, consolidated
>   `90ef9a8eb7` into `collect_def_time_default_checks`): defaults are evaluated
>   at the def's SOURCE-ORDER position across all three def-execution paths
>   (module pass / ClassDef methods / nested defs), closing the last deferred
>   soundness residual (`default-arg-raises-sound` + `-nested-method-sound` +
>   `-ordering-nofp` CORE; source-order emission dissolves the earlier spike's
>   `def f(a=G[1])` false alarm).
> - Precision in the same arc: boxed TUPLE recognised as subscriptable
>   (`24f7f6b3b8` — tuple params no longer raise a spurious "not subscriptable"
>   TypeError; extraction stays sound-nondet, LIST-tag-guarded) and
>   `reversed()` constant-folds over a constant list (`836a3b3445`, mirroring
>   sorted(), so `tuple(reversed(const))` folds).
> Milestone: **no known false proofs on any axis** (both standing gates, WIDE
> negated + WIDE value sweeps, oracle) **and no deferred soundness residuals**.
> Three KNOWNBUGs promoted to CORE this arc (`slot-pun-list-element-knownbug` →
> `-subscript-sound`, `slot-pun-attr-field-knownbug` → `-external-sound`,
> `default-arg-raises-knownbug` → `-sound`).
>
> **Continuation (2026-07-14 → 2026-07-16) — the real-world corpus campaign.**
> Running the 51-benchmark AWS/boto3 suite (python-verification-benchmarks)
> end-to-end drove the suite from a de-poisoned baseline of CLEAN 18 / FP 17 /
> TOERR 8 to **CLEAN 34 / TP 7 / FP 4 / TOERR 0 / OOM 0**, surfacing and fixing
> several classes no fuzzing axis had reached:
> - **UNSAT-vacuity GLOBAL false-proof class** (`9434661bbf`, `b70b52746d`): a
>   string-emitter call site that omits the enclosing-function scope gives its
>   solver output symbols GLOBAL identity; re-execution (loop iterations — the
>   long-known form — or a TWICE-CALLED function) asserts conflicting content
>   constraints over one SSA value → the refinement formula goes UNSAT →
>   **every property in the program vacuously SUCCESSFUL**. Six vectors closed
>   (binop concat, str(int), str(float), str.strip, subscript-slice ×2-audited);
>   `emit_string_function`'s in_loop/scope parameters lost their DEFAULTS so
>   every call site decides explicitly (compiler-enforced invariant, documented
>   at the emitter). The universal audit probe: the op in a twice-called
>   function + `assert False` (provable only if UNSAT). PLR-guardrail tradeoffs
>   taken soundly: `re-ignorecase-dotall-flags` downgraded to KNOWNBUG (scoping
>   the slice site costs regex-flags precision — a sound false alarm);
>   `github_3553`'s historical sweep PASS was PROVEN vacuous (the old binary
>   verifies it with `assert False` appended) and re-baselined. CORE pins:
>   `string-{concat,of-int,of-float,strip,slice}-twice-called-function`.
> - **safe_address_of** (`fd7824adb6`): a method call through a CLASS-LEVEL-
>   annotated field reads the receiver via the shadow-fallback ternary; taking
>   a plain address_of of that if_exprt crashed symex ("address_arithmetic:
>   non-persistent array") — 8/51 benchmarks crashed, ONE signature. The helper
>   distributes address-of over if_exprt arms (both genuine lvalues, preserving
>   reference semantics), materialising a temp otherwise. CORE
>   `class-attr-field-method-call`.
> - **Handler-aware obligations (PLR §8.4)** (`fc21afef88`, `d97fe0dfde`):
>   `exception_is_caught` now honors catch-alls (`except Exception` catches —
>   the lint-style carve-out contradicted PLR); the not-subscriptable TypeError
>   obligations raise catchably when an enclosing handler covers TypeError
>   (definite property otherwise); and the call-argument tag obligation became
>   a CONDITIONAL CATCHABLE TypeError at the binding + a GUARDED bind (unwrap
>   on tag-match, nondet otherwise — never a silent pun). A pure guarded-nondet
>   variant WITHOUT the exception was tried and REJECTED: it re-opened the 004
>   false proof. CORE `typeerror-caught-by-handler`(+`-uncaught-definite`);
>   pins `method-arg-tag-obligation`/`param-coercion-typeerror` updated to the
>   exception-based property.
> - **PEP 649 version-dependent annotations** (`418377d4af`): the AST payload
>   carries `_python_version`; a >= 3.14 parser enables lazy (future_annotations)
>   semantics automatically, <= 3.13 keeps the eager def-time NameError. Found
>   via 26/51 corpus programs using unimported annotation names (vacuously
>   SUCCESSFUL under --python-no-exception-checks). CORE
>   `annotation-nameerror-eager-312`.
> - **Dict[str, object] lowering** (`011b11b7b0`): non-"safe" dict value types
>   lowered to python_int_type() — a dict modelled as an int (latent
>   unsoundness + the dominant corpus FP cluster). Now dict[K, python_value]
>   for safe keys, python_value otherwise; enabled by the Any-unbox model-bound
>   assumption and the mutation-havoc moving to pending_post_checks. CORE
>   `dict-object-value-annotation`.
> - **Precision**: sys.argv modelled (nondet list[str]; guarded access precise,
>   unguarded may-IndexError — CORE `sys-argv-guarded`/`-unguarded-oob`);
>   module-attribute reads return non-constant module-global LVALUES; nondet
>   stub initialisers register typed valueless globals; pv-CLASS `__getitem__`
>   single-owner dispatch with shape-based key binding (CORE
>   `pv-class-getitem-dispatch`), the dispatch serving as the fallback arm of
>   both int- and string-key paths.
> Remaining real-world residuals (tracked in the private findings repo): a
> 4-FP **pv-provenance/iteration** family (a wholly-nondet python_value flowing
> into the subscript tag obligation via pv-CLASS iteration; four `__iter__`
> dispatch attempts failed to propagate element/length constraints and were
> REVERTED — the principled fix is per-instance element provenance, the same
> architectural family as the generator-object model, plan §1), two stub-heavy
> timeouts, and MISS-by-unreachedness cases (a stub CONTRACT fires only if the
> buggy method is called; whole-program semantics).

> **Proactive-sweep finding (2026-06-30):** a targeted adversarial sweep of
> under-tested corners (beyond the oracle corpus) found a **generator
> consumption-state / identity** false-proof cluster: a generator consumed
> through anything other than a direct `next(name)`/`name.send()` on its
> original Name re-yields already-consumed elements. **The `for`-loop and alias
> channels are now CLOSED (2026-07-01):** `for x in g` resumes from `g`'s cursor
> (`gen-foriter-after-next-typeerror`, CORE) and an alias `it2 = it` shares the
> cursor (`gen-alias-consume-typeerror`, CORE). `list(g)`/`sum(g)` after a partial
> `next()` are now CLOSED too (2026-07-02 — cursor-aware aggregating builtins,
> `gen-aggregating-after-next` CORE). The container slot `box[0]` is now CLOSED
> too (2026-07-06, `1f6b9d3905`): `next()` on a stored channel returns sound
> nondet (`gen-in-container-consume`, CORE). The whole generator consumption-state
> cluster is now sound; a fully precise generator-OBJECT model remains future work
> (plan §1). (Async, symbolic-key dict, and escaping closures probed sound in the
> same sweep.)
>
> **Sweep round 2 (2026-07-01)** — a broader batch (~40 probes over
> mutation-during-iteration, exception/`finally`, identity/`is`, numeric
> coercion, comprehension scope, aliasing, unpacking, MRO, float edges) found the
> frontend **sound on all of those** (each correctly FAILED), plus **two more
> false-proof roots** (both since **CLOSED** — see below):
> - **Container-literal cross-type numeric dedup** (one root): a set literal
>   `{1, 1.0}` / `{1, True}` and a dict literal `{True: 1, 1: 2}` over-counted
>   distinct elements/keys because the literal builder deduped with
>   type-sensitive equality instead of Python numeric equality
>   (`1 == 1.0 == True`). **CLOSED 2026-07-01** via a shared `python_numeric_key`
>   helper (canonical integer for int/bool/integral-float) used by both the
>   set-literal bitmap and `build_dict_value`; now CORE `set-literal-numeric-dedup`,
>   `dict-literal-numeric-dedup`. (The *incremental* paths — `set.add`, `d[k]=v`,
>   `dict.update`, `frozenset(list)`, `dict(list-of-pairs)` — were already sound.)
>   `build_dict_value` also deduped a repeated **string** literal key by value
>   (**CLOSED 2026-07-01**, `dict-literal-string-key-dedup` CORE — a python_string
>   is a struct, not a `constant_exprt`, so exact-expr equality never merged
>   them; `{"a":1,"a":2}` had over-counted len and read the first value). So the
>   dict/set literal builders now dedup **all** constant-key kinds by Python
>   equality: numeric, string (by value), exact for others; symbolic keys are
>   not deduped (sound).
> - **Star-unpack call arity**: `f(*[1, 2, 3])` into a 2-parameter `f` was not
>   flagged. **CLOSED 2026-07-01**: `validate_call_signature` now folds a
>   statically-known `*`-unpack length (list/tuple literal) into the positional
>   count (too-many AND too-few); a Name-bound unpack / vararg callee is not
>   flagged (sound). Now CORE `star-unpack-call-arity` (+ `-nofp`).
> - Additional generator-cluster channels confirmed (same root as the
>   consumption-state cluster): `list(gen)` / `sum(gen)` after a partial `next()`
>   also re-yielded — **CLOSED 2026-07-02** (cursor-aware aggregating builtins).
> A precision *false alarm* (not a false proof) was also seen: `1.0 in {1}` is
> not proven (membership over-approximates a cross-type numeric hit).
>
> **Sweep round 3 (2026-07-01)** — another batch (~35 probes over string/slice
> edges, the numeric tower, custom iterator protocol, exception/`finally`/`else`
> flow, `__slots__`/inheritance, `@property`, format, dict-ordering, hashing,
> chained/augmented assignment) found the frontend **sound on most** (each
> correctly FAILED), plus **five more false-proof roots**, now pinned KNOWNBUG:
> - **`__slots__` not enforced** — assigning or reading an attribute not in a
>   class's `__slots__` should raise AttributeError. **STORE CLOSED 2026-07-01**
>   (`slots-not-enforced`, CORE): `class_slots` + `slots_forbidden_attr` flag an
>   attribute STORE on a slots-enforced class (all MRO user-bases declare
>   __slots__). **READ CLOSED 2026-07-02** (`slots-read-missing-attr`,
>   `slots-read-nofp`, CORE): the read side (`slots_read_forbidden`, both the
>   pointer/self and struct/value base paths in `convert_attribute`) raises
>   AttributeError for a read that is not a slot / method / class-attr (own or
>   inherited) / object-dunder; properties/descriptors resolve earlier
>   (`fae9d5db4f`).
> - **`__eq__` without `__hash__`** — such a class's instances are unhashable, so
>   a set/dict-key use raises TypeError. **CLOSED 2026-07-01**
>   (`eq-without-hash-unhashable`, CORE): `class_eq_without_hash` +
>   `is_unhashable_type` (the shared lever, so all hashability sites benefit;
>   the last two hardcoded checks were routed through it).
> - **Augmented-assignment type errors** — `x += y` applies the binary operator,
>   so `int += str` / `list += int` / `str += int` should TypeError, but the
>   AugAssign path skipped the binary-op type check. **CLOSED 2026-07-01**
>   (`augassign-type-error`, CORE): convert_aug_assign reuses the shared
>   `binop_operand_type_error` predicate factored from convert_bin_op.
> - **Chained-assignment aliasing** — `a = b = <mutable>` binds BOTH targets to the
>   SAME object; the frontend bound independent copies, so a mutation through one
>   was invisible to the other. **CLOSED 2026-07-01** for mutable CONTAINERS
>   (`chained-assign-aliasing`, CORE): convert_assign materialises the value in
>   the first target and aliases the rest (pointer + alias_targets). **Chained
>   INSTANCE assignment `a = b = C()` CLOSED 2026-07-02**
>   (`chained-assign-instance-alias`, `chained-assign-nofp`, CORE): convert_assign
>   rewrites `a = b = <value>` to `a = <value>; b = a; …` so the later targets go
>   through the proven single-target `b = a` alias path — mutable objects
>   (instance / list / dict) alias, immutables copy, value evaluated once
>   (`e5e8b61e2c`).
> - **Read-only `@property` assignment** — assigning to a getter-only property
>   raises AttributeError. **CLOSED 2026-07-01** (`property-readonly-assign`,
>   CORE): emit_property_set raises AttributeError when the attr is a property
>   with no setter across the MRO.
>
> **Sweep round 4 (2026-07-02) — PLR-tagged differential fuzzer.** A template
> generator of PLR-tagged programs (now ~450 across binop/augassign/compare/
> dedup/hash/slots/property/chained/generator/arity/unpack/tryflow/seqindex/
> dictops/numedge/iterproto/strmethod/classedge/slicing/fstring/kwargs/closure/
> withctx/matchstmt/excflow/compvar/metaclass) runs CPython vs cbmc and classifies
> FALSE_PROOF / FALSE_ALARM / AGREE, gated against a committed baseline of known
> false-proof labels (a standing soundness-regression gate alongside the oracle).
> It drove a false-proof count from 73 (first run) down through the closures
> below; the remaining are all deferred-with-plan or planned (see the rows). Each
> closure landed validation-gated (target flips, suite green, sweep 2719/0-reg,
> oracle 0-NEW):
> - **Operand-type verdict unified + extended** (`981b546971`): the duplicated
>   `convert_bin_op` inline check + `binop_operand_type_error` were merged into one
>   `compute_binop_verdict(op,l,r)` → {ok, nondet, error}. Extended (PLR §6.7): a
>   PROVABLE `None` operand to any arithmetic/bitwise op → TypeError; a bitwise/
>   shift op with an int-like operand and any non-int-like, non-`set` operand →
>   TypeError (`1 & [1]`, `1 & 'a'`; `list` is EXCLUDED — a non-int set literal is
>   modelled as a python_list so `set|set` must stay nondet, hence the residual
>   list-bitwise row below). CORE `binop-none-operand`, `binop-bitwise-dict`; the
>   aug-assign path now models `list += <iterable>` as extend (`augassign-list-
>   iterable-nofp`).
> - **Compare cross-category ordering** (`bf26f21979`, PLR §6.10.1): `<`/`<=`/`>`/
>   `>=` between two operands of different orderable categories (numeric / str /
>   list / tuple / set / dict / None) → TypeError (`1 < [1]`, `None < 1`). Placed
>   EARLY in `convert_compare` (before the numeric fast-path) via
>   `orderable_category_of`; same-category and Any/class operands are never
>   flagged. CORE `compare-cross-category-typeerror`, `-nofp`.
> - **Comparison-reduction mixed-category whole-group** (`2d76730fb9`,
>   `845a831cac`, PLR §6.10.1): `sorted()`/`min()`/`max()` compare elements
>   pairwise, so a list spanning 2+ distinct orderable categories (int vs str,
>   int vs list, ...) → TypeError. All three now share one
>   `constant_list_orderable_conflict` helper (previously only `sorted` had a
>   narrower numeric-vs-str-only inline check, and `min`/`max` had none). The
>   helper resolves a Name bound to a list literal (so `xs=[1,"a"]; min(xs)` is
>   caught, not only `min([1,"a"])`); no `key=` (a key remaps compared values);
>   Any/symbolic elements are never flagged (FP-free). Found by the property-based
>   random fuzzer (rand_169). CORE `min-max-mixed-category`, `-nofp`.
> - **List-concat constant-fold whole-group** (`2eceff5929`, PLR §6.3.2/§3.2):
>   list `+` of two constant literals (directly, or a Name via `list_literals`)
>   now folds to a merged CONSTANT struct instead of an opaque symbolic one, with
>   element-type promotion to `python_value` when operand element types differ
>   (via `rebuild_list_as_pv`, so `list[int] + list[str]` merges without a
>   malformed array). Concatenation always makes a fresh list, so folding is sound
>   regardless of operand aliasing. Effect: length, content, index bounds, and
>   sorted/min/max mixed-category detection all track through `a + b` (and
>   homogeneous `xs = xs + [..]`). CORE `list-concat-fold-mixed`, `-oob`, `-nofp`.
>   The mixed-element VARIABLE reassignment (`xs=[8,8]; xs=xs+["c"]`) is now also
>   CLOSED (`c9d592fed8`, CORE `min-max-mixed-concat`): a reassignment whose RHS is
>   a list whose element type widened to python_value RETYPES the binding instead
>   of casting it back to the narrower slot (which dropped the widened content).
>   **List-length/identity residual — now CLOSED (`e766dde276`):** min/max-empty
>   via a comprehension over range(NON-constant bound) then slice
>   (`min-empty-symbolic-comprehension`, CORE) -- the unroll-based comprehension
>   now models a symbolic-bound range length (exact with no filter, `[0,bound]`
>   with a filter). This was the last open item of the **list-length/identity-
>   tracking** whole-group (distinct from the reference-identity family); both
>   whole-groups are now closed.
> - **In-place list mutation invalidates the constant-fold snapshot whole-group**
>   (`8d0dbf3d0f`, `e1e7e6cfc9`, PLR §3.3, SOUNDNESS): a list mutation updates the
>   runtime list but must also invalidate the `list_literals` snapshot, else
>   `sorted`/`min`/`max`/`index`/the mixed-orderable check constant-fold against
>   PRE-mutation data -> false proof (`xs.append(-5); assert min(xs) == 0` proved
>   SUCCESSFUL). Now ALL in-place list mutations erase the snapshot: methods
>   (append/insert/extend/remove/pop/clear) and subscript/slice/aug-subscript
>   stores; sort/reverse maintain it in place. Found by PRECISION TRIAGE of the
>   value-oracle fuzzer's false alarms -- the fuzzer cannot catch this class
>   directly (it asserts CPython's correct value, so a stale-fold is a false
>   ALARM, not a proof); CORE `list-mutate-invalidates-fold`,
>   `list-substore-invalidates-fold` are the regression guard. Also restored
>   precision (append-then-op). CORE `-nofp` variants.
> - **Tuple subscript bounds whole-group** (`bb58c69c1e`, PLR §6.3.2): `t[i]` with
>   i outside `[-len, len)` raises IndexError. The constant-index path already
>   flagged OOB, but a NON-constant/computed index (`t[sum(xs)]`, `t[len(...)]`)
>   returned nondet with NO check -> false proof. A tuple's arity is static, so
>   emit the same symbolic IndexError check lists use (normalise negatives, then
>   `0 <= eff < len`). Found by the property-based random fuzzer (21 of 25 false
>   proofs in one run). CORE `tuple-index-computed-oob`, `-nofp`.
> - **Self-assignment self-pointer fix** (`9aa080d426`, PLR §3.1): `x = x` is a
>   no-op, but the local-alias by-reference transform (`b = a` -> `b =
>   address_of(a)`) fired on it, binding `x = address_of(x)` -- a self-referential
>   pointer that corrupted the object: OOB index checks stopped firing (false
>   proof) and `len(x)`/`x[i]` became nondet (false alarms). Both alias-transform
>   sites now skip when the resolved target equals the LHS symbol; genuine
>   aliasing (`b = a`, chained `c = b`) is unaffected. Found by the property-based
>   random fuzzer. CORE `list-self-assign-alias`, `-nofp`.
> - **Non-iterable scalar whole-group** (`a20166d97f`, PLR §3.3.1): iterating or
>   unpacking a PROVABLY non-iterable scalar (concrete numeric / complex / constant
>   None) → TypeError, via one shared `provably_non_iterable_scalar` predicate at
>   ALL three sites — `for`-loop, tuple/list unpack, and comprehension (previously
>   only the for-loop flagged numeric scalars, missing None/complex, and unpack /
>   comprehension had no scalar check). CORE `noniterable-for-none`,
>   `noniterable-unpack-scalar`, `noniterable-comprehension`, `noniterable-nofp`.
> - **Container-key canonicalization generalised** (`f3449ecbbc`, PLR §3/§6.2):
>   the earlier per-type dedup patchwork (numeric / string / None / tuple-
>   STRUCTURAL / exact) in `build_dict_value` AND the string-only set-literal dedup
>   were replaced by ONE recursive `canonical_key` over the full constant lattice —
>   numeric cross-type, non-integral float, str value, None singleton, and tuples
>   ELEMENT-WISE (recursing). Closes cross-type-numeric tuple keys
>   (`{(1,2):a,(1,2.0):b}` → 1), None-set dedup (`{None,None}` → 1), the set
>   tuple-xtype analogue, and nested tuples. CORE `dict-tuple-key-xtype-dedup`,
>   `set-none-tuple-xtype-dedup`. A symbolic key has no canonical form and is never
>   merged (sound).
> - **`__slots__` read + chained-instance alias** — see the corrected bullets
>   above (both closed 2026-07-02).
>
> **Deferred with a sound, PLR-grounded plan** (NOT shipped — the fuzzer's
> remaining false proofs, each spiked this session):
> - **list-bitwise** (`1 & [1]` etc., 10 labels) — **SOUNDNESS CLOSED 2026-07-02**
>   (`1d15e1d7de`, `binop-int-bitwise-list`, `binop-bitwise-set-nofp` CORE) as the
>   spike-gated Phase 1: an int-like operand bitwise-combined with a REAL list
>   (`python_list` NOT `#python_set_semantic`-tagged, not a bitmap set) is a
>   TypeError; a set LITERAL is excluded so `set|set` / `frozenset(..)|{..}` stays
>   nondet. The full distinct-type refactor is still DEFERRED (it ripples into
>   type identity across ~180 `is_python_list_type` sites and is entangled with
>   the `frozenset()`/set-union "typed as nondet-int" warts), but that refactor is
>   now needed only for the set-union / set-comprehension *precision* residuals
>   (inventory B), not the soundness. See
>   [plan §0: set-representation spike](python-frontend-plan.md#false-proofs).
> - **plain-class `missing_attr`** (`c.missing` on a `__dict__` class) —
>   **CLOSED 2026-07-02** (`3bc104028b`, `plain-missing-attr-read`,
>   `plain-missing-attr-nofp` CORE). Whole-group lever: a program-wide
>   `assigned_attr_names` set (every Store-context `X.attr=` target) +
>   `class_attr_set_closed` (no `__getattr__`/`__getattribute__`/metaclass/
>   decorator, known bases, no `setattr`/`__dict__`/`vars`). Flags `c.attr` iff
>   `attr` is not a component/method/class-attr/dunder AND not in
>   `assigned_attr_names`, on a closed-set class — FP-free (the unannotated-param
>   / alias / decorator stores all put the name in `assigned_attr_names`).
> - **generator `box[0]` container slot** — `list(g)`/`sum(g)`-after-`next()` are
>   now CLOSED (2026-07-02, cursor-aware aggregating builtins); the generator
>   stored in a container slot (no Name to key the cursor, copied by value) is now
>   CLOSED for SOUNDNESS too (2026-07-06, `1f6b9d3905`: `next()` on a stored
>   channel returns sound nondet, `gen-in-container-consume` CORE). A fully
>   precise per-object cursor for that channel remains future work,
>   [plan §1](python-frontend-plan.md#generators).
>
> A few new PRECISION false alarms (sound direction) were also catalogued — see
> inventory B: star-args tuple unpack `f(*t)`, set-comprehension dedup length.
>
> **Sweep round 5 (2026-07-02) — del / generator lifecycle.** The fuzzer's del/
> generator-lifecycle category found three false proofs:
> - **`gen.close()` then `next()`/`for`** — a closed generator raises
>   StopIteration. **CLOSED** (`b999ef340d`, `gen-close-then-next` CORE): close()
>   exhausts the consumption cursor, so the existing next()/for StopIteration
>   guard fires.
> - **`del x` then read** — NameError. **CLOSED** (`7af40cffb5`, `del-name-use`,
>   `del-name-nofp` CORE): a per-name `<qname>$deleted` flag (set on del, cleared
>   by any assignment, checked at reads via convert_name → conditional NameError)
>   replaces the old None-reset approximation. Control-flow-precise (not-taken
>   conditional del / reassignment are clean) and off the hot path for
>   non-del-tracked names.
> - **`del c.a` then read** — AttributeError. **PINNED KNOWNBUG**
>   (`del-attr-read-knownbug`): a sound read-raise needs PER-INSTANCE deleted
>   state (a scope-local per-name flag is unsound under aliasing — `del c.a; d=c;
>   d.a` must still raise), which is the per-instance-identity / Phase-4 family.
>   **→ later CLOSED (2026-07-06, `41d6788612`)** via a per-instance
>   `__present_<attr>` flag (independent of Phase-4); test now `del-attr-read`
>   (CORE).

> **Sweep round 6 (2026-07-03) — feature-combination fuzzing.** Combination
> templates (feature A x B, where interaction bugs hide) found ONE new false
> proof: **`del` of a closure-captured variable** — `def f(): x=5; def g(): return
> x; del x; return g()` raises NameError in CPython (capture is by CELL, so `del`
> unbinds the shared cell) but cbmc verifies SUCCESSFUL. Root: the frontend
> captures a free variable BY VALUE, so the nested function reads a stale copy
> unaffected by `del`; the DIRECT-read del case is caught (`del-name-use` CORE),
> but the closure sub-case needs the by-reference cell-capture model (the
> [fat-closure plan](python-frontend-fat-closure-plan.md)). PINNED KNOWNBUG
> (`del-closure-nameerror-knownbug`). **→ later CLOSED (2026-07-06, `be037ec01a`)**
> via a deleted-flag guard at the capture-read (no cell-capture needed); test now
> `del-closure-nameerror` (CORE). Precision-only combination findings (sound
> FALSE ALARMS, not proofs): a comprehension over a partially-consumed generator
> is not cursor-aware (the list/sum cursor work does not extend to the
> comprehension consumer yet), and `a=[1]; b=a; a+=[2]` does not propagate the
> in-place extend to the alias (list aug-assign aliasing).

- *Closed 2026-06-30 (commit `70401b6d90`):* `gen_send_before_start` — the eager
  generator cursor already encodes priming (`cursor == 0` ⟺ not started), so a
  new `.send()` handler raises `TypeError` for a non-None send to a just-started
  generator; en route a whole-group fix made `yield` *expressions* (`x = yield`)
  count toward `__gen_result` alongside `yield` statements
  ([plan §1 Phase 1 OUTCOME](python-frontend-plan.md#generators)). Now CORE:
  `gen-send-before-start-typeerror`, `gen-send-prime-nofp`. A genuine
  generator-object identity model (aliasing / container / `for`-after-`next`)
  remains future work and **IS a known false-proof cluster** — see the
  proactive-sweep note above (consumption-state).
- *Closed 2026-06-30 (commit `27bb327b26`):* `dec_not_callable` +
  `dec_wrong_arity` — the frontend now models bare-`@name` decorator
  application (`@d` → `f = d(f)`): a provably non-callable decorator raises a
  def-time TypeError, and a decorated call dispatches through the wrapper so its
  arity is enforced ([plan §15 OUTCOME](python-frontend-plan.md#decorators)).
  Now CORE: `dec-not-callable-typeerror`, `dec-wrong-arity-typeerror`,
  `dec-callable-nofp`.
- *The intrinsic / out-of-subset residuals* (marked `ORACLE-INTRINSIC`, NOT
  default-subset bugs; **3 as of 2026-07-13**): the annotation-laundering pair —
  `004` (arg boundary), `ty-010` (return-annotation) — caught under
  opt-in `--python-check-annotations`; and `d1` (int→float coercion at a call
  boundary, documented OUT of the PyHard subset). The former third laundering
  member `007` (list-element via append) is now **CLOSED** by the slot-pun
  element widening (see the 2026-07-10/13 note). See inventory D.
- *Whole-groups COMPLETED this session* (each landed validation-gated: target
  flips, full suite green, by-value sweep 2719 PASS / 0 regressions, oracle
  0-NEW). All have CORE lock-in tests:
  - **dunder-protocol-missing** (one `class_mro_defines` lever, 9 sites):
    subscript read (`__getitem__`) / store (`__setitem__`) / delete
    (`__delitem__`), iteration `for`/comprehension/unpack (`__iter__`, with the
    `__getitem__` sequence fallback), membership (`__contains__`), call
    (`__call__`), context-manager `with` (`__enter__`/`__exit__`), unary
    (`__neg__`/…), binary operators (`__add__`/reflected `__radd__`/…), and
    ordering (`__lt__`/…). A concrete user-class instance lacking the dunder
    raises TypeError; Any/symbolic/builtin/inherited never flagged.
  - **format-spec validation** (`format_code_violation` + `value_format_category`,
    3 sites): `%`-format (`%d % "x"` → TypeError), `str.format` and f-string
    (`{:d}` on str/float → ValueError, `{:s}` on int → ValueError). Constant
    spec + concrete value only.
  - **comparison/ordering**: ordering missing-dunder (above); mixed-category
    element comparison (`[1,2] < [1,"a"]` via static lexicographic eval;
    `sorted([1,"a"])`; concrete `list[int] < list[str]`) → TypeError.
  - **hashability** (`is_unhashable_type`): an unhashable (list/dict/set) dict
    key / set element at dict-literal / dict-comp / `d[k]=` / `set.add`/`discard`
    → TypeError.
  - **@dataclass construction** (`build_class_construction`): the synthesised
    `__init__` binds constructor args to the annotated fields at all four
    construction sites (assignment, expression, `return`, `with`) and clears the
    spurious unassigned-field AttributeError — closed ~59 false ALARMS.
  - **dunder-return contracts** (`dunder_return_type_violation`): `__len__`/`__str__`
    return-type, plus a constant-negative `__len__` value → ValueError.
  - **builtin-edge family** (constant-operand gated): `divmod(x,0)`,
    `ord(multichar)`, `round(str)`, `sum([str])`, `",".join([int])`, `int(inf)`/
    `int(nan)`, `str.encode(bad-codec)`, `for x in <scalar>`.
  - **soundness corner-cases**: `set().pop()` (a masking-`assume` that cut the
    empty path), `len()` negative-`__len__`, and `x + g()` left-operand
    evaluation order (module-global snapshot before a side-effecting right).

The dated narrative below records how this state was reached (oracle baseline
journey: a 2026-06-25 audit; the 2026-06-28/29 reference-semantics arc; two
2026-06-29 differential-audit rounds 6→22 discovery; then 22→3 via the
whole-groups above). Where a dated note's running tally disagrees with this
header, **this header is current**.

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
  - **CLOSED (2026-06-26) — missing TypeError for non-numeric operand shapes**:
    a concrete **float operand to a bitwise/shift/invert** operator (`1.0 & 2`,
    `1.0 << 2`, `~1.0`) and **sequence × float** repetition (`"abc" * 2.5`,
    `[1] * 2.0`) now raise TypeError via the whole-group operand-type obligation
    (`float-bitwise-typeerror`, `sequence-mul-float-typeerror`).
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
returning a separate iterator object) is NOT flagged. Tests `iterate-no-iter`,
`comprehension-no-iter`, and the assign-unpack site (`a, b = C()`, `[*C()]`,
a structurally different handler) `unpack-no-iter` — all CORE. **Residual:** a
`__getitem__`-only class is correctly not flagged but its iteration is still
modelled imprecisely (zero iterations).

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
  would FP on symbolic-but-nonneg returns). *(The negative-VALUE case was LATER
  CLOSED via a constant-only check -- `len-negative-dunder` CORE; see header.)*
- *Builtin arg/edge preconditions: an irreducible per-builtin FAMILY, not a
  single whole-group.* Each member has a distinct predicate (ord: one-char
  string; divmod: nonzero divisor; round: numeric arg; join: all-str elements;
  sorted: mutually-comparable; encode: valid codec). The SHARED lever is
  `emit_conditional_exception(predicate, exc)` gated on a CONSTANT operand (so
  symbolic operands are never falsely flagged). Done: `divmod` (ZeroDivisionError)
  and `ord` (single-char TypeError, `builtin-ord-multichar` CORE). The rest
  (round/join/sorted/encode) remain tracked in the oracle corpus as individual
  family members.
- *Format-spec validation (`"%d" % x`, `"{:d}".format(...)`, `f"{x:d}"`):*
  Requires modelling format-string parsing and per-conversion type rules.
  *(LATER CLOSED -- the format-spec whole-group is complete across all three
  sites; CORE `format-spec-{percent,strformat,fstring}`; see header.)*

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

**Soundness corner-case closes (2026-06-30, cont.) -- oracle false proofs 7 -> 3.**
Per "soundness is the utmost priority even for single-digit corner cases":
- *`set_pop_empty`* -- the set.pop KeyError on `bm==0` was being MASKED: a
  subsequent `assume (bm & bit) != 0` (the nondet member) is `0 != 0` on the
  empty path, silently cutting it. Added a `bm == 0` disjunct so the empty path
  stays feasible and the KeyError fires. (A masking-assume soundness bug -- worth
  watching for elsewhere.)
- *`len_nonint`* -- value-axis of the dunder-return contract: a `__len__` that
  provably returns a negative integer constant (detected at class definition) ->
  ValueError. Constant-only, so a symbolic/non-negative `__len__` is never
  flagged (no FP). CORE `len-negative-dunder`.
- *`binop_mirror_evalorder`* -- `x + g()` reads the left BEFORE the right; a
  module-level GLOBAL left read is now snapshotted before a side-effecting right
  call (a callee can rebind a global but not a caller local, so the gate is
  module-scope only -- which also avoids breaking recursive-call convergence
  like `n * fact(n-1)`). CORE `binop-evalorder-left-snapshot`.
- *`d1_numeric_tower`* reclassified ORACLE-INTRINSIC -- the int->float coercion
  at a call boundary is documented OUT of the PyHard subset.

**Remaining 3 false proofs are all FEATURES, not point fixes:**
*(All three CLOSED later on 2026-06-30 — see the CURRENT STATE header; commits
`27bb327b26` decorators + `70401b6d90` generator-send. This entry records the
triage at the time.)*
`dec_not_callable` + `dec_wrong_arity` need general decorator-application
modelling (above); `gen_send_before_start` needs a generator state machine
(generators are currently modelled as EAGER `__gen_result` lists with no
generator-object identity / priming state / `.send()` semantics). Each is a
dedicated feature. The standing oracle 0-NEW gate keeps these from regressing and
guards against new false proofs.

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
| binop eval-order **MIRROR** | `x + g()` where `g()` mutates a value read by the LEFT operand: cbmc read `x` AFTER `g()` (lazy symbol ref) rather than before (PLR left-to-right), so a wrong value could false-prove. The FORWARD case `g() + x` was already fixed | **CLOSED 2026-06-30** (`binop-evalorder-left-snapshot`): a module-level GLOBAL left read is snapshotted before a side-effecting right call. Gated to module scope — a callee can rebind a global but not a caller local — which also avoids the recursive-unwind regression the broad version caused (`n * fact(n-1)`); closes `binop_mirror_evalorder` | — |
| Variable-length-tuple slice → str method (ty-010) | `def f(t) -> str: return t[1:]` then `f(...).upper()`: the **`-> str` return annotation is trusted**, so `f(...)` is typed `str` and `.upper()` is accepted even though the body returns a tuple/list | **Annotation-laundering family (same as 004/007), flag-gated.** Caught under `--python-check-annotations` — the *return*-annotation-mismatch check fires on `return t[1:]` (a sequence vs declared `str`). DEFAULT trusts the annotation (declined for the same measured ~1.25% benign-FP reason as 004/007); pinned `vartuple-slice-strmethod-knownbug` (which slices a DIRECT literal `(1,2,3)[1:]` to exercise the laundering cleanly). **Update (2026-07-10):** the earlier `def f(t: tuple[...]): return t[1:]` form was masked by a spurious "not subscriptable" TypeError on the tuple param — that false alarm is now CLOSED (TUPLE added to the python_value subscriptable-tag set, `tuple-param-subscript-nofp` CORE), so a tuple param is subscriptable; the laundering itself remains the flag-gated residual | [plan §7](python-frontend-plan.md#check-annotations) |
| dict `del` through a call (c2) | `del p["x"]` inside `rm(p)` does not propagate the deletion to the caller`s dict, so a later `pt["x"]` misses the `KeyError` | **OPEN, deep**: dicts are by-reference for EXISTING-key value modifications but NOT for STRUCTURAL mutations — adding a key or deleting one does not cross the call boundary (keys/length arrays not shared); same representation limit as dict-value-byref | [dict-byref](python-frontend-dict-value-byref-plan.md) |
| `del obj.attr` + `__getattr__` fallback (c4/laurel-006) | after `del self.x`, access falls to `__getattr__` returning a different type; the concretely-typed field could not hold it, so `del` havocked the slot to nondet (sound for a stale read, but it missed the cross-type `TypeError`) | **CLOSED 2026-06-29** (`getattr-after-del`): a field `del self.x`-ed in a method of a `__getattr__`-class is typed `python_value`, and `del` stores `__getattr__`'s result into the slot, so the cross-type use routes through the operand obligation and raises `TypeError`. The deletable-field scan is module-wide, so both `del self.x` (in a method) and a direct `del f.x` (external instance) are covered. Present-int read / del-then-reassign / non-deleted field / no-`__getattr__` class all correct. **Residual:** `del o.x` through an Any-boxed FUNCTION PARAMETER does not propagate (the del handler cannot resolve the param's class) -- the structural-mutation-through-a-call family (c2) | [plan §10](python-frontend-plan.md#descriptors) |
| **Narrowing-invalidation cluster** (CLOSED 2026-06-28) | a value's runtime type changes via an effect cbmc does not model, then it is used at the stale type → CPython `TypeError`, cbmc verifies. Distinct roots (**not one fix**). **CLOSED:** `__setattr__`/`__getattribute__` (2026-06-25, over-approx to nondet `python_value`); **enum `.value` after a member retag** (2026-06-28: heterogeneous-enum value type is `python_value` + `.value` resolves on enum-typed variables AND fields, so the retagged value routes through the operand/tag obligations — `enum-value-after-mutation` is now CORE). **CLOSED 2026-06-28 (reference-semantics-for-instances):** context-manager `__enter__`/`__exit__` field mutation AND composition aliasing were the SAME *concrete-class-typed slot copies the instance* root, now fixed (instance fields are by-reference) (a `t: SomeClass` param/field is value-copied, so a mutation through it is invisible to the original; the Any-typed path preserves identity by-address). Also OPEN: inheritance+union virtual dispatch, same-expression **eval-order × union-retag** | partly **UNSOUND** (the reference-semantics false proofs remain), confined to advanced/dynamic features; the reference-semantics cases AND the eval-order case (`union-use-after-mutation-typeerror`, 2026-06-28: a side-effecting binop left operand is now sequenced before a right read so a same-expression union retag is observed) are now CORE. **Fully CLOSED (2026-06-28).** The *mirror* eval-order case (`x + g()` where the side-effecting operand is on the RIGHT and mutates a value read on the LEFT) is now also **CLOSED 2026-06-30** (`binop-evalorder-left-snapshot`; module-scope left snapshot — see the "binop eval-order MIRROR" row) | [plan §0](python-frontend-plan.md#false-proofs) |
| pv-provenance / iteration (real-world residual) | a WHOLLY-NONDET `python_value` (e.g. the loop element of a pv-CLASS iteration over a stub response object) flowing into the subscript tag obligation false-alarms — the tag genuinely might be non-container. Four `__iter__`-dispatch attempts failed to propagate element/length constraints across the dispatched view and were reverted; the principled fix is PER-INSTANCE ELEMENT PROVENANCE, the same architectural family as the generator-object model. 4 boto3 benchmarks (ecs/ses/athena + the bedrock capacity bound) | [plan §1](python-frontend-plan.md#generators) |
| Any/union used at a wrong type | a tagged-union/`Any` value used as a concrete type with a mismatched runtime tag now raises `TypeError` via **tag obligations** at the operator, subscript, and (provenance-gated) call-argument boundaries — was a silent wrong-field read. The remaining hole is the call-argument obligation only firing for **explicitly-annotated** scalar params (inferred/Any params excluded to avoid false alarms) | sound (closed for the three covered sites); see the tag-obligation table in [Type-coercion at boundaries](#any--union-tag-obligations-typeerror-on-a-wrong-runtime-tag) | [plan §0](python-frontend-plan.md#false-proofs) |
| Definite integer overflow (default 64-bit) | the default 64-bit model silently **wrapped** on a statically-provable >64-bit result (`10**19 < 0`, `1<<70 == 0`) — a false proof. Now reports `python-model-bound` (assert+assume cut) on a DEFINITE overflow; a symbolic/computed overflow remains the documented 64-bit bound (use `--python-unbounded-ints`, now sound incl. shifts/bitwise) | sound (definite cases reported; symbolic = documented bound) | [plan §0](python-frontend-plan.md#false-proofs) |
| Unknown annotation fallback | `convert_type_annotation` lowered an annotation it could not model (bare `range`/`tuple`, unmodeled builtin, unknown forward-ref) to `python_int_type()`, modeling an unknown value with concrete int semantics — a latent unsoundness that could mask a real bug | **CLOSED 2026-06-26**: unknown ⇒ `python_value` (Any/top), the sound over-approximation (sweep gained `ethereum_bug-fail`). Lock-in `check-annotations-unknown-is-any`. **Exception still open:** a dict with a non-"safe" value type (`dict[str, Any]`) still falls back to int — an opt-in `--python-check-annotations` false positive (not a false proof), blocked by a separate Any-valued-container capacity-model-bound issue; pinned `check-annotations-any-dict-knownbug` | [plan §9](python-frontend-plan.md#precision) |
<!-- 2026-06-26: the blocker is the SYMBOLIC-KEY dict precision cluster, not a standalone capacity bug -- an Any dict value makes d.get(k,...) a nondet key, so a later d2[key].append(...) over-approximates and fires a spurious python-model-bound (github_3684 class). -->
| Class-instance identity / aliasing | a class instance was value-semantics (struct copy) at a top-level `b = a` assignment, so a mutation through one alias was invisible to the other. Distinct from the list/dict case, which already aliased (the `alias_targets` pointer mechanism). | **CLOSED 2026-06-28** (reference-semantics-for-instances, Phases 1-3): instances are by-reference at returns, local aliases, and fields -- so return-flow, local alias `b=a`, AND composition/field store all preserve identity. `instance-return-aliasing`, `instance-aliasing`, `instance-field-aliasing`, `shared-object-aliasing`, `context-manager-enter-mutation` are all CORE. **Extended (a2):** an ANNOTATED alias `r: C = o` now pointer-promotes too (`convert_ann_assign`), and an lvalue instance crossing an Any/`python_value` boundary preserves identity by `address_of` for ALL lvalue forms (symbol / dereference / member / index), not just a plain symbol — so `f(r)` where `r=o` no longer passes a throwaway copy (`instance-annotated-alias-call`; closes a2_narrowing_alias). Soundness: a FRESH construction stays owned/by-value (distinct, no over-aliasing). The deep-composition `==` perf cliff did NOT materialise, so this is default-on (unlike container ref-semantics) | [instance ref-semantics plan](python-frontend-instance-reference-semantics-plan.md) + [plan §0](python-frontend-plan.md#false-proofs) |
| Nested mutable-element aliasing | anonymous nested-mutable list/dict elements stored by value; the replication / self-append / new-container channels are **guarded** (report `python-model-bound`). **Direct** nested mutation works (`c[i].append(...)` for lists, and int-keyed dict values via lvalue value slots). **Extraction-then-mutate** (`r=c[i]; r.append(...)` / `v=a[k]; v.append(...)`) is **guarded** in the default config (2026-06-24): the extracted variable is recorded (`extracted_container_alias`) and, on a subsequent in-place mutation, the source container is havoced + dropped from the const-fold maps (sound over-approximation). **For LISTS, the principled whole-group fix is now implemented behind opt-in `--python-ref-mutables`** (2026-06-24): nested list elements are heap-allocated per instance and aliased by pointer (`make_python_value(LIST, allocate_boxed_leaf(rebuild_list_as_pv(e)))`), so extraction / multi-instance / reorder / membership / nested value reads / 3-deep composition are **precise**; under the flag the havoc guard is skipped for wrapped references. | sound for the CONTAINER-ELEMENT aliasing channels (guarded). **The distinct
object-identity-via-COMPOSITION channel** (one object referenced by two
attributes, mutated through one and read through the other) **is now CLOSED**
(`shared-object-aliasing`, CORE — fixed by reference-semantics-for-instances,
2026-06-28; instance fields are by-reference). With `--python-ref-mutables` the
nested-list cases below become precise (validated: §4 gate green, 5
genuinely-false negatives stay FAILED, default suite green). **Confirmed OPT-IN ONLY (2026-06-25):** making it the default is **not viable** — precise nested-list `==` under reference semantics needs per-element pointer deref that blows up pointer analysis (depth-2 already TIMEOUTs; corpus needs 3-deep `==`), and the A/B sweep showed regressions (PASS 2710 vs by-value 2715 incl. a crash). The value-vs-reference tradeoff is fundamental in BMC; by-value + sound guards stays the default. Dicts/sets remain guarded. | [ref-semantics spike §10–§12](python-frontend-reference-semantics-spike.md) + [plan §0](python-frontend-plan.md#false-proofs) + [dict-byref](python-frontend-dict-value-byref-plan.md) |
| dict-literal const-fold | the dict-subscript const-fold substituted a construction-time-tracked value at a later read; for a value embedding a **mutable** symbol (a reassignable variable, a boxed-leaf pointer) this re-read the current value → false proof (`n=1; d={"k":n}; n=2; d["k"]`). **Closed 2026-06-24** (`value_is_const_foldable`): the const-fold now fires only for invariant values | sound (closed) | — |
| Native `smt_string`→int cast | under `--python-smt-strings`, a spurious str→int coercion from value plumbing lowers to `str.to_int` (defined-but-approximate); genuine `int(str)` is exact | sound (approximate, flagged) | [strings plan](python-frontend-strings-plan.md#strings) |
| BMC bounds | bugs deeper than `--unwind` / beyond the bounded container or 64-bit ranges are not found | sound w.r.t. the bound (intrinsic to BMC) | — (intrinsic) |

### B. Imprecisions (sound; spurious failures / nondet over-approximation)

| Area | Gap / deviation | Plan |
|---|---|---|
| Generators | list-with-cursor model: inter-yield side-effect ordering is eager (not faithful); the *value sent in* via `gen.send(v)` is not passed into the pending `yield` expression (the priming-state TypeError IS modelled — see soundness/Generator semantics). **Consumption-state / identity is a known false-proof cluster, NOT a mere imprecision — see section A** (`for` after partial `next`, alias, container slot). (The earlier "module-global free vars in a generator `if`" and "cross-boundary list-shape" residuals are resolved.) | [plan §1](python-frontend-plan.md#generators) |
| Closures | **escaping** closures over-approximate captured free vars to nondet (late binding `lambda: i` in a loop imprecise); non-escaping + `nonlocal` mutation + capture-through-param are correct | [plan §2](python-frontend-plan.md#closures) + [fat-closure deep-dive](python-frontend-fat-closure-plan.md) |
| Strings (refined default) | ordering, substring `replace`, `split`, `casefold`/`title`, symbolic `count` — sound-but-imprecise; all precise (or precise-able) on the **native** backend opt-in | [strings plan](python-frontend-strings-plan.md#strings) |
| Regex | symbolic-subject and negated-membership (`re4`/`re11`) imprecise/slow on refined; precise on native. **2026-07-16:** the DOTALL/combined-flags precision (`re-ignorecase-dotall-flags`) is KNOWNBUG — scoping the subscript-slice string-emitter site (a soundness fix, see the UNSAT-vacuity note) costs it; re-promote when the re-intrinsics' string view stops depending on global emitter symbols | [strings plan §4](python-frontend-strings-plan.md#regex) |
| Lists | symbolic-list precision cluster (`nondet_list*`, `list_extend*`, `list-sort*`) — spurious failures; `list.count(x) == N` over a concrete list is not proven (precision) | [plan §9](python-frontend-plan.md#precision) |
| Tuples | fixed-tuple **slicing** `t[1:]` is now modelled exactly for constant bounds (step 1, negatives, `[::-1]`; `tuple-slice` CORE) — **was** mis-modelled (wrong `len`/index). Remaining: SYMBOLIC-bound or general-step tuple slices fall through (over-approx); a `tuple[int, ...]` variable-length sequence model (vs Any) is unbuilt — a precision item (the element value read from a boxed/variadic tuple is a sound nondet). **CLOSED (2026-07-10):** a `tuple[...]`-annotated OR plain **parameter** (a TUPLE-tagged python_value) is now correctly subscriptable — indexing/slicing no longer raises a spurious "not subscriptable" TypeError (`tuple-param-subscript-nofp` CORE); TUPLE was missing from the python_value subscriptable-tag set. `tuple.count`/`index` exact-value asserts not always proven | [plan §9](python-frontend-plan.md#precision) |
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

| Call / `*args` value unpack | `f(*t)` VALUE binding — **CLOSED 2026-07-02** (`star-arg-value-unpack`, `55d3efaf5a`): a tuple `*`-unpack was misread with list layout; now spreads the tuple's element values (list `*xs` and literal `*[..]` were already handled). Arity check unchanged | — |
| Sets (list-backed) | a NON-int set is modelled as a `python_list`, so (a) `{"a"} \| {"b"}` and other set-algebra ops (`\|`/`&`/`-`/`^`) over list-backed sets over-approximate length/contents (spurious FAILED on a `len`/membership assert), and (b) a **set comprehension** `{e for …}` routes through the list-comprehension path WITHOUT element dedup, so its `len` is over-counted (spurious FAILED). Both sound-direction; found by the PLR fuzzer (`kwargs`/`compvar/setcomp_ok`). Same list-backed-set root as the list-bitwise item whose SOUNDNESS half is now closed (§A, Sweep round 4); this PRECISION face needs the set-union modelling / representation work that was deferred | [plan §9](python-frontend-plan.md#precision) |
| Comparison RESULT (same-category) | `bool < int` (`True < 2`) and `set < set` subset (`{1} < {1,2}`) — **CLOSED 2026-07-02** (`compare-bool-set-result-precision`, `d4e0e27c7b`): bool promoted to int for ordered comparisons (PLR §6.10.1 subtype), set ordering modelled as the bitmap subset predicate (PLR §3.2) | — |

| Fuzzer precision residuals (rounds 5–6, 2026-07-02) | Sound-direction FALSE ALARMS with DISTINCT roots, each low-priority: (a) **comprehension over `range(param)`** — `def f(n): sum([i*i for i in range(n)])` verifies FAILED because the function summary converts the comprehension once with `n` symbolic (not inlined at `f(3)`) — a function-summary limitation, not a comprehension bug (a CONSTANT-range comp is precise); (b) **walrus-in-comprehension** length; (c) **bytearray** — `len(bytearray(b"ab"))` proves 2 but a spurious uncaught exception fires; (d) **custom `__iadd__`** in-place result; (e) **Any-param store→read value** — `def f(o): o.x=5; return o.x` returns nondet not 5 (the attribute-error store FP is closed; the VALUE does not propagate, per-instance-identity family). **CLOSED 2026-07-03 (`bc13e1f145`):** builtin `pow(a,b[,m])` (→ **/% operator lowering, `builtin-pow-precision` CORE) and `zip(a,b)` (→ precise list of tuples, `builtin-zip-precision` CORE). All tracked by the PLR-fuzz false-alarm list | [plan §9](python-frontend-plan.md#fuzz-precision) |

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
| Annotation-laundering (default mode) | an Unknown/unannotated value crossing a TRUSTED annotation boundary raises only at runtime — argument boundary (`004`) and return annotation (`ty-010`). The former list-element member (`007`, via `append`) is **CLOSED in default mode** (2026-07-10, slot-pun element widening — the element is kept `python_value` so the misuse faults). Default mode trusts static annotations by design for the remaining pair (enforcing them would false-positive on harmless wrong-but-unused annotations); caught under opt-in `--python-check-annotations` (CORE `annotation-call-arg-wrong-type`, `check-annotations-list-append`, `annotation-return-wrong-type`). Marked `ORACLE-INTRINSIC` in the differential corpus | [plan §7](python-frontend-plan.md#check-annotations) |
| Numeric-tower at a call boundary (`d1`) | an `int` passed where `float` is declared then a float-only method (`x.hex()`): the int→float coercion at a call boundary is documented OUT of the PyHard subset. (Within the subset, a float-only method on a CONCRETE int receiver is caught — `method-on-wrong-type`.) Marked `ORACLE-INTRINSIC` | — |

### E. NO CURRENT PLAN (explicitly flagged)

Each item below is classified as either **not a PLR gap** (cosmetic / tool-output
alignment — soundness and semantics are already correct) or a **genuine but
low-priority semantic gap** with a one-line PLR-grounded sketch (a full plan is
deferred as disproportionate to the value, per the standing worklist; none of
these is a *soundness* hole — all are sound over-approximations or cosmetics):

- **Call-signature error-message formatting** — *NOT a PLR gap.* The frontend
  soundly detects missing/duplicate/unknown args (emitting a generic uncaught
  `TypeError`), but does not reproduce ESBMC's exact `TypeError: foo() missing …`
  / `Properties: N verified` strings, so those `github_30xx`/property-count sweep
  tests stay DIFF rather than PASS. This is tool-output alignment, not a
  semantic/PLR deviation — no PLR grounding applies; no plan (low value).
- **`*args` + required keyword-only** signature combinations — *semantic gap
  (sound: a missing kwonly is not flagged).* PLR §4.8.2/§8.6: a keyword-only
  parameter with no default MUST be supplied. *Sketch:* extend
  `validate_call_signature` so the vararg branch still runs the
  required-kwonly-present check (it currently short-circuits when `*args` is
  present). Low priority (rare).
- **Native `casefold`/`title`** (Unicode case-mapping has no SMT-LIB primitive)
  and **symbolic `count`** — tracked as residuals in the
  [strings plan](python-frontend-strings-plan.md#strings) (their home doc), no
  concrete SMT encoding yet.
- **Cross-module `**d`-of-mutated-dict** and **non-dict cross-module global
  mutation** — *semantic gap (sound: over-approximated to nondet / stale).* PLR
  §7.6/§4.2.2: a module global mutated in module A and `**`-unpacked or read in
  module B must observe the mutation. *Sketch:* the cross-module global-dict
  channel already exists for dict-literal mutation; extend it to the `**d` unpack
  read and to non-dict globals (share the module-global symbol rather than a
  per-module copy). Rare; deferred. See [plan §5](python-frontend-plan.md#dict-byref)
  / [§6](python-frontend-plan.md#modules).

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
| New class-constructor call site | Call `build_class_construction(class_name, self_lvalue, call_ast, loc)` and add the returned statements | The unified chokepoint: emits the `__init__` call (MRO walk + arg conversion + kwarg matching + default padding + boundary coercion via `build_class_init_call`), OR the synthesised `@dataclass` field binding when there is no explicit `__init__`. Used by all four construction sites (assignment, expression, return, with) |
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
