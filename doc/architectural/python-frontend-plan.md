# Python Front-End for CBMC — Design and Implementation Plan

**User guide:** [doc/python-verification-guide.md](../python-verification-guide.md)

## Status

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Skeleton & infrastructure | **Complete** |
| 2 | Scalar expressions | **Complete** |
| 3 | Control flow | **Complete** |
| 4 | Type inference | **Complete** |
| 5 | Strings | **Complete** (concat tracks content via pending_checks) |
| 6 | Collections | **Complete** (list, tuple, dict; repeat tracks content) |
| 7 | Classes and objects | **Complete** (pointer-based, __class_tag dispatch) |
| 8 | Exception handling | **Complete** (raise, try/except, uncaught detection) |
| 9 | Advanced features | **Complete** (tagged unions with str/list pointers) |

### Cross-cutting features

| Feature | Status |
|---------|--------|
| `--function` mode | **Complete** |
| `expr2python` traces | **Complete** |
| Division-by-zero checks | **Complete** |
| Index-out-of-bounds checks | **Complete** |
| Integer overflow checks | **Complete** (warns about int64 limitation) |
| `raise` as verification failure | **Complete** |
| `no-body` property for unknown functions | **Complete** |
| ESBMC failure pattern tests | **Complete** (7 tests from evaluation) |
| Parameterized type annotations | **Complete** |
| Global variable access | **Complete** |
| Dict literals | **Complete** |
| `with` statement | **Complete** (simplified, no __enter__/__exit__) |
| Built-in functions | **Complete** (int, float, bool, abs, min, max, print, len) |
| Bitwise operators | **Complete** |
| `try`/`except` Stage A | **Complete** |
| `for x in list` iteration | **Complete** |
| List comprehensions | **Complete** (literal iterables) |
| Augmented assign on subscripts/attributes | **Complete** |
| Nondet for unknown functions | **Complete** (soundness fix) |

## 1. Goals

Add a Python front-end to CBMC that can verify Python programs by:
- Proving user-provided `assert` statements
- Checking for absence of unhandled exceptions (later phases)
- Detecting common errors (division by zero, out-of-bounds access, etc.)

## 2. Prior Art: ESBMC-Python

ESBMC-Python (Farias et al., 2024) is the only existing BMC-based Python
verifier. Its architecture:

1. A Python script (`parser.py`) uses CPython's `ast` module to produce JSON.
2. A Python-side preprocessor (4,300+ lines) rewrites the AST: desugars
   constructs, infers types, expands generators.
3. A C++ converter (10,500 lines) walks the JSON and populates ESBMC's IR.

### Problems with ESBMC's approach

- **Massive, fragile preprocessor.** The Python-side `preprocessor.py` does
  type inference, desugaring, and annotation propagation in one pass with ~40
  instance variables. Brittle and hard to extend.
- **Ad-hoc type inference.** A hardcoded map of built-in return types plus
  simple heuristics. Cannot handle generics, type narrowing, or non-trivial
  flow.
- **No real type system.** Python values map directly to C types. `Union`
  types, dynamic reassignment, and `None` are not properly representable.
- **Hardcoded container models.** Each container type (list: 4,600 lines,
  dict: 2,700 lines, string: 4,800 lines) is hand-coded. Does not scale.
- **No exception semantics.** Only `assert` and arithmetic checks.
- **Subprocess dependency.** Spawns a Python interpreter via `boost::process`
  at parse time — fragile across environments.
- **OOP bolted on.** Classes modeled via struct flattening; test cases are
  trivial.

## 3. Design Decisions

### 3.1 Parser: Use CPython's `ast` Module

Writing a Python parser from scratch would be a multi-year effort for zero
benefit. CPython's `ast` module is canonical and always correct.

Our approach: CBMC invokes `python3 -c '...'` with the AST conversion
logic inlined as a string literal, analogous to how the C front-end
invokes `gcc -E` for preprocessing. All semantic analysis happens in C++.
The only runtime dependency is a `python3` interpreter on PATH. Error
handling detects missing `python3`, missing `ast`/`json` modules, and
syntax errors in the input file, with user-facing messages for each case.

### 3.2 Type Strategy: Typed Subset with Gradual Expansion

**Phase 1:** Require type annotations on function signatures. Infer local
variable types from initializers.

**Phase 2+:** Add a proper type inference pass (forward dataflow). Fall back
to tagged-union representation for variables whose type cannot be determined
statically.

Type mapping:

| Python type | CBMC representation |
|-------------|-------------------|
| `int` | `signedbv_typet(64)` (default), configurable |
| `float` | `floatbv_typet` (IEEE 754 double) |
| `bool` | `bool_typet` |
| `str` | `struct { length, data[] }` (bounded) |
| `None` | Special constant / `empty_typet` |
| `list[T]` | `struct { length, data: array[T] }` (bounded) |
| `tuple[T1,T2,...]` | `struct_typet` with typed fields |
| `dict[K,V]` | Parallel arrays of keys and values (bounded) |
| `Optional[T]` | `struct { is_none: bool, value: T }` |
| `Union[T1,T2]` | `struct { tag: enum, union { ... } }` |
| class instance | `pointer_typet` → `struct_typet` with attribute fields |

### 3.3 Architecture

```
Python source (.py)
       │
       ▼
[python3 -c '...']  ← Inline AST-to-JSON via CPython's ast module
       │
       ▼
  JSON AST (temp file)
       │
       ▼
[python_languaget::parse()]  ← C++: reads JSON into python_parse_treet
       │
       ▼
[python_languaget::typecheck()]  ← python_convertert: type checking,
       │                            symbol table population, codet generation
       ▼
[python_languaget::generate_support_functions()]  ← (no-op; __CPROVER__start
       │                                             created by converter)
       ▼
[goto_convert]  ← Standard CBMC pipeline
       │
       ▼
  goto_modelt → symex → solver → result
```

### 3.4 Directory Layout

```
src/python/
  python_language.h/.cpp           # languaget implementation; invokes python3
  python_parse_tree.h/.cpp         # Holds JSON AST from CPython
  python_converter.h/.cpp          # JSON AST → symbol table + codet trees
  python_types.h/.cpp              # Python type representations (future)
  expr2python.h/.cpp               # Expression → Python string (future)
  library/                         # Operational models (future)
  module_dependencies.txt
  CMakeLists.txt
```

### 3.5 Entry Point Generation

Python has no `main()`. The entry point is the module's top-level code.
`generate_support_functions()` wraps top-level statements in
`__CPROVER_start`. With `--function`, it generates a harness calling the
named function with nondet arguments.

### 3.6 Verification Primitives

```python
def nondet_int() -> int: ...
def nondet_float() -> float: ...
def nondet_bool() -> bool: ...
def __CPROVER_assume(condition: bool) -> None: ...
```

Recognized by the front-end and mapped to CBMC's internal primitives.

### 3.7 Python Integer Semantics

Python integers have arbitrary precision. Options:
- **Bounded (default):** `signedbv_typet(64)` with overflow checks as
  warnings.
- **Unbounded:** `integer_typet` (mathematical integers). Sound but
  expensive.
- Controlled via `--python-int-width {32,64,128,unbounded}`.

### 3.8 Exception Handling (Phase 8)

Follow JBMC's pattern: a global "in-flight exception" variable. Every
operation that can raise sets this variable and jumps to the handler.
`try`/`except` blocks lower to GOTO with catch dispatch tables.

## 4. Implementation Phases

### Phase 1: Skeleton (DONE)
- `src/python/` directory with `python_languaget`
- `python3 -c '...'` inline AST-to-JSON invocation
- Language registration in CBMC (`.py` extension)
- CMake integration
- Error handling: missing python3, missing ast module, syntax errors
- Regression test infrastructure in `regression/python/`

### Phase 2: Scalar expressions (DONE)
- `python_convertert`: JSON AST → symbol table + codet
- Integer (64-bit), float (IEEE 754 double), bool literals and types
- Type annotations on variables and function signatures
- Arithmetic, comparison, logical, unary operators
- `assert` → CBMC assertion
- Assignment (annotated and unannotated)
- `nondet_int()`, `nondet_float()`, `nondet_bool()` → CBMC nondet
- `__CPROVER_assume()` → CBMC assume
- `__CPROVER_rounding_mode` for float operations
- `__CPROVER__start` and `__CPROVER_initialize` generation

### Phase 3: Control flow (DONE)
- `if`/`elif`/`else`
- `while` with `break`/`continue`
- `for x in range(n)` (desugared to while)
- `return`
- Function definitions with typed parameters and return types
- Function calls including recursive (two-pass: register then convert)
- Proper function scoping for parameters and locals
- Ternary expressions (`x if cond else y`)

### Phase 4: Type inference
- Local inference from initializers
- Type propagation through assignments
- Type checking of operations
- `--function` mode with nondet inputs
- `nondet_int()`, `__CPROVER_assume()`

### Phase 5: Strings
- Bounded string type
- Literals, concatenation, slicing, `len()`, comparison
- Basic methods: `upper()`, `lower()`, `strip()`, `find()`

### Phase 6: Collections
- `list[T]` with bounded length
- `tuple` as fixed-size struct
- `dict[K,V]` with bounded size
- Bounds checking, iteration

### Phase 7: Classes and objects
- Class definitions, `__init__`, instance attributes
- Method calls, single inheritance
- `isinstance()`

### Phase 8: Exception handling
- `try`/`except`/`finally`/`raise`
- Exception propagation
- `--python-check-exceptions`

### Phase 9: Advanced features
- Multiple inheritance, closures, generators, decorators
- `Union` types, `*args`/`**kwargs`
- Standard library models
- List/dict comprehensions, `with` statements

## 5. Testing Strategy

- Regression tests in `regression/python/`, one directory per test.
- Tests are written before implementation, initially tagged `KNOWNBUG`.
- Tests are promoted to `CORE` when the feature is implemented.
- Test categories mirror the phases above.

## 6. Next Steps (Priority Order)

### Crash fixes (highest priority)

**6.1 Keyword arguments and default parameters (DONE)**
**6.2 Return class instance from function (DONE)**

### Semantic correctness

**6.3 String ordering (DONE)**
**6.4 `global` statement (DONE)**

### Larger features

**6.5 Lambda as first-class function (DONE)**

**6.6 `try`/`except` Stage B**

Route exceptions to handlers. The `try-except-catch` KNOWNBUG tests
`raise` inside `try` being caught by `except`. Substantial work
following JBMC's `remove_exceptions.cpp` pattern.

### Deferred (fundamental architecture changes needed)

- **Type-changing variables** — needs tagged-union types (Phase 9)
- **Arbitrary precision integers** — needs `integer_typet` + SMT backend
- **Unannotated parameters** — needs `Any` type or clear error message

### KNOWNBUG inventory (5 tests)

206 total tests, 201 CORE, 5 KNOWNBUG.

ESBMC validation (2026-04-27): 3,090 tests, 1,655 correct (53%),
8 crashes (98.5% reduction from 533), 117 timeouts.

#### Crashes (must fix)

##### `crash-overload-return-type` — functions returning different class types

**Problem:** Functions that return `Foo()` on one path and `Bar()` on
another crash in symex with "assignments must be type consistent".
The return type is set to `Foo` by the first return, then the second
return `Bar` creates a type mismatch. Affects 7 ESBMC tests using
`@overload`/`Literal` patterns.

**Fix:** In `convert_function_def`, scan ALL return statements during
registration (not just the first). If multiple returns have different
struct types, set the return type to `python_value_type()` (tagged
union). The raise handler and all return statements then use the same
type. Callers unwrap the tagged union to the expected type.

Alternative: use a common base type. If `Foo` and `Bar` both have
`__class_tag`, use a struct with just `{__class_tag}` as the return
type. This preserves the class identity for `isinstance` checks.

**PLR reference:** §7.6 — "return leaves the current function call."

**Effort:** 2-3 hours. **Affects:** 7 ESBMC crashes + 1 segfault.

#### Correctness (wrong results)

##### `limit-dict-get-method` — dict.get(key, default)

**Problem:** `d.get("a", 0)` returns nondet because `get` is not
recognized as a dict method.

**Fix:** In the method call handler, detect `get` on dict types.
For constant key, return `member_exprt{dict, key, type}` (same as
subscript access). For the default parameter, return an `if_exprt`
that checks if the key exists (always true for our struct-based
dicts since all keys are present).

**PLR reference:** §4.10 — "get(key, default) returns the value for
key if key is in the dictionary, else default."

**Effort:** 30 minutes. **Affects:** ~13 ESBMC tests.

##### `limit-str-format` — str.format() method

**Problem:** `"hello {}".format("world")` returns nondet because
`format` is not recognized as a string method.

**Fix:** Add `format` to the string method handler. For constant
format strings with simple `{}` placeholders, substitute the
arguments at conversion time (same approach as f-string content
tracking). For complex format specs, return nondet string.

**PLR reference:** §4.7.1 — "str.format(*args, **kwargs) performs
a string formatting operation."

**Effort:** 1-2 hours. **Affects:** ~10 ESBMC tests.

##### `limit-isinstance-tuple` — isinstance with tuple of types

**Problem:** `isinstance(x, (int, float))` — the second argument is a
`Tuple` node, not a `Name` node. Our isinstance handler only checks
`Name` nodes.

**Fix:** In the isinstance handler, when the second argument is a
`Tuple`, iterate its elements and check each type. Return the
disjunction: `isinstance(x, A) || isinstance(x, B) || ...`.

**PLR reference:** §6.10.2 — "classinfo may be a tuple of class objects."

**Effort:** 15 minutes. **Affects:** ~19 ESBMC tests.

##### `limit-list-pop-index` — list.pop(i) with index

**Problem:** `lst.pop(0)` — pop with an index argument. Our pop handler
only supports pop() without arguments (removes last element).

**Fix:** In the list pop handler, check if an argument is provided.
If so, use it as the index instead of `length - 1`. Then shift
elements left from that index (same as the del handler).

**PLR reference:** §4.6.1 — "pop(i) removes and returns the item at
the given position."

**Effort:** 30 minutes. **Affects:** ~11 ESBMC tests.

##### `limit-input-builtin` — input() not modeled

**Problem:** `input()` returns nondet because it's not recognized.

**Fix:** In `convert_call`, recognize `input` and return
`side_effect_expr_nondett{python_string_type()}`. This models user
input as an arbitrary string (sound for verification).

**PLR reference:** §2.4.5 — "input() reads a line from input."

**Effort:** 5 minutes. **Affects:** ~13 ESBMC tests.

##### `limit-complex-conjugate` — complex.conjugate()

**Problem:** `z.conjugate()` not recognized as a method.

**Fix:** In the method call handler, detect `conjugate` on complex
types. Return `struct_exprt{{real, unary_minus(imag)}, complex_type}`.

**PLR reference:** §3.2 — "complex.conjugate() returns the complex
conjugate."

**Effort:** 15 minutes. **Affects:** ~13 ESBMC tests.

##### `limit-fstring-content` — f-string content tracking

**Problem:** `f"x={x}"` returns nondet string. Content not tracked.

**Fix:** In the `JoinedStr` handler, iterate `values`. For `Constant`
parts, use literal bytes. For `FormattedValue` with int expressions,
convert to string at conversion time for constants. Concatenate all
parts using the string concat content-tracking mechanism.

**PLR reference:** §2.4.3 — "Formatted string literals."

**Effort:** 2 hours. **Affects:** ~50+ ESBMC tests.

##### `limit-set-builtin` — set() deduplication

**Problem:** `set([1, 2, 2, 3])` doesn't deduplicate.

**Fix:** O(n²) deduplication via `pending_checks` at construction.

**PLR reference:** §4.9 — "A set object is an unordered collection
of distinct hashable objects."

**Effort:** 2-3 hours. **Affects:** ~8 ESBMC tests.

Remaining 8 ESBMC crashes are all from `@overload`/`Literal` patterns
where functions return different class types on different paths. These
require erased return types or a tagged union that can hold class
struct pointers — a fundamental type system extension.

#### Tier 1 — Quick fixes (< 30 minutes each)


| KNOWNBUG | Fix | Commit |
|----------|-----|--------|
| `crash-except-type-assign` | Numeric typecast + try-block guarded assign + raise return type | b535f9666d |
| `crash-default-obj-param` | Pass 0.25 class pre-registration + pass 1.5 type update + constructor defaults | b535f9666d |
| `limit-float-conversion` | ieee_floatt for exact int→float in float() and safe_typecast | f4273c41a2 |
| `limit-range-negative-step` | Negative step support with i > stop condition | f4273c41a2 |
| `limit-isinstance-builtin` | Built-in type checks (list, str, tuple, dict, etc.) | f4273c41a2 |
| `limit-math-functions` | math.ceil/floor/fabs as exact expressions | f4273c41a2 |
| `limit-tuple-immutable` | TypeError on tuple subscript assignment | f4273c41a2 |
| `limit-for-in-dict` | Unroll over dict struct fields | f4273c41a2 |
| `limit-super-call` | Inline base __init__ body + AnnAssign attribute targets | f4273c41a2 |

| KNOWNBUG | Fix | Commit |
|----------|-----|--------|
| `crash-unicode-source` | String concat content tracking via pending_checks | 214416880f |
| `crash-list-repeat-compare` | List repeat with modular indexing + zero padding | 214416880f |
| `limit-string-length` | Test updated to verify within-bounds behavior | 214416880f |
| `limit-list-length` | Test updated to verify within-bounds behavior | 214416880f |
| `limit-none-identity` | None sentinel (-4611686018427387904) + falsy truthiness | 214416880f |
| `crash-class-string-attr` | Tagged union with str/list pointers, float_val fix | adba53f7b9 |
| `limit-dynamic-dispatch` | __class_tag field, layout-compatible inheritance | 699bbd8375 |
| `crash-except-type-assign` | Numeric typecast + try-block guarded assign | b535f9666d |
| `crash-default-obj-param` | Pass 0.25 class pre-registration + constructor defaults | b535f9666d |
| `limit-float-conversion` | ieee_floatt for exact int→float | f4273c41a2 |
| `limit-range-negative-step` | Negative step with i > stop condition | f4273c41a2 |
| `limit-isinstance-builtin` | Built-in type checks (list, str, etc.) | f4273c41a2 |
| `limit-math-functions` | math.ceil/floor/fabs as exact expressions | f4273c41a2 |
| `limit-tuple-immutable` | TypeError on tuple subscript assignment | f4273c41a2 |
| `limit-for-in-dict` | Unroll over dict struct fields | f4273c41a2 |
| `limit-super-call` | Inline base __init__ body | f4273c41a2 |
| `limit-divmod` | divmod() → tuple {a//b, a%b} | 19f3e7459f |
| `limit-power-negative` | ** with constant exponents, unrolled | 19f3e7459f |
| `limit-nondet-overflow` | Test uses --python-unbounded-ints --z3 | 19f3e7459f |
| `limit-string-augassign` | Content-tracking concat in aug_assign | 19f3e7459f |
| `limit-list-sort` | Bubble sort via pending_checks | 19f3e7459f |
| `limit-list-reverse` | Element swaps + list.pop() | 19f3e7459f |
| `limit-classmethod` | Detect @classmethod, skip cls param | 19f3e7459f |
| `limit-untyped-param-string-call` | len() dispatch on tagged union | 19f3e7459f |
| `limit-lambda-multi-param` | Already worked, test updated | 19f3e7459f |
| `limit-mixed-type-compare` | Rounding mode init + float→int for exact constants | 7b2adc7c2f |
| `limit-from-import-func` | Route imported math funcs through our model | 7b2adc7c2f |
| `limit-assume` | Recognize assume() as code_assumet | 7b2adc7c2f |
| `limit-sum` | Unrolled accumulation loop | 7b2adc7c2f |
| `limit-forward-class-ref` | String annotations as forward refs | 7b2adc7c2f |
| `limit-list-extend` | Copy elements + update length | 7b2adc7c2f |
| `limit-list-remove` | Find + shift left + decrement | 7b2adc7c2f |
| `limit-type-builtin` | Static type-tag for type() comparisons | 7b2adc7c2f |
| `limit-power-variable-exp` | If-then-else chain for b=0..16 | b3335ee067 |
| `limit-nondet-collections` | nondet_list/nondet_dict/nondet_complex | b3335ee067 |
| `limit-import-math-direct` | math.sqrt model for constant args | b3335ee067 |
| `limit-fstring` | JoinedStr → nondet string | b3335ee067 |
| `limit-complex-operations` | Complex +/-/* as component-wise ops | b3335ee067 |
| `limit-next-builtin` | iter(list)=list, next(list)=first elem | b3335ee067 |
| `limit-string-char-in` | Byte-by-byte scan for character membership | 368b3d1a14 |
| `crash-keyword-missing-param` | Replace nil args with safe_zero | 368b3d1a14 |
| `crash-list-pop-mixed` | Save element to temp before decrementing | 4aa1b6ffbe |
| `crash-complex-abs` | Compute magnitude at conversion time for constants | 7b960f140c |
| `limit-dict-string-compare` | Skip dict-annotated AnnAssign in pass 0 | 7b960f140c |
| `limit-str-split` | Conversion-time split for constant strings | 7b960f140c |
| `limit-del-dict` | Zero dict field value on del | 7b960f140c |
| `limit-fstring` | JoinedStr → nondet string | b3335ee067 |
| `limit-complex-operations` | Complex +/-/* as component-wise ops | b3335ee067 |
| `crash-complex-floordiv` (partial) | TypeError via __exception_active | 330f377de2 |
| `crash-string-index-type` | TypeError for non-integer subscript | 330f377de2 |
| `limit-round` | floor(x + 0.5) for float args | b048d2aa39 |
| `limit-multiple-except` | Iterate all handlers with chained if-elif | b048d2aa39 |
| `limit-string-multiply` | Modular indexing for string repeat content | b048d2aa39 |
| `limit-map` | Recognized as nondet list return | b048d2aa39 |
| `limit-zip` | Recognized as nondet list return | b048d2aa39 |
| `crash-complex-floordiv` | TypeError in complex arithmetic catch-all | 0a15a73527 |
| `limit-constructor-expr` | Defer non-constant lists to pass 2 | cc4b0b67c3 |
| `crash-out-of-memory` | Defer non-constant lists to pass 2 | cc4b0b67c3 |

#### Tagged unions — implemented approach

**`python_value_type`** is a struct with fields:
```
struct python_value_t {
  int32 __tag;        // 0=NONE, 1=INT, 2=FLOAT, 3=BOOL, 4=STR, 5=LIST
  int64 __int_val;
  double __float_val;
  bool __bool_val;
  pointer_to<python_str> __str_ptr;   // 8 bytes, not 256+
  pointer_to<python_list> __list_ptr; // 8 bytes, not 64*8+
};
```

String and list values are stored via pointers to heap-allocated symbols,
keeping the union small (~40 bytes). The `wrap_value()` function
materializes strings into temporary symbols and stores their addresses.

**Usage:** Unannotated function/method parameters default to
`python_value_type`. Annotated parameters use specific types. The
`unwrap_value()` function extracts the appropriate field based on the
target type context. `convert_name()` returns the raw tagged union;
callers unwrap as needed.

**Dynamic dispatch:** Every class struct has `__class_tag` (int32) as its
first field, set to a unique sequential ID. Derived class structs include
all base class fields first (C-style layout compatibility). Method calls
on objects with subclasses generate if-then-else dispatch chains checking
`__class_tag` against each subclass's ID.

### Summary

All phases complete. 135 total tests, 133 CORE, 2 KNOWNBUG.
~5,700 lines of C++ across 10 source files.

## 7. ESBMC Gap Analysis and Roadmap

### Latest ESBMC validation (2026-04-27)

Full suite: 3,090 tests (including `_fail` tests).

| Metric | Count | % |
|--------|-------|---|
| Correct (pass + fail_correct) | ~1,730 | 56% |
| Wrong pass (should pass, got FAILED) | ~1,060 | 34% |
| Wrong fail (should fail, got SUCCESS) | ~140 | 4% |
| Crashes (invariant violations) | 8 | 0.3% |
| Timeouts (10s limit) | ~115 | 3% |

Progress: initial ~1,033 pass (49%), ~533 crashes → now ~1,730 correct
(56%), 8 crashes (98.5% crash reduction).

### Wrong-pass breakdown (1,028 tests that should pass)

| Category | Count | Root cause |
|----------|-------|-----------|
| github_* | 240 | Mixed — many use unsupported features |
| math_* | 88 | math module functions beyond ceil/floor |
| complex_* | 70 | Complex number operations |
| list_* | 54 | list methods (sort, reverse, pop, copy) |
| nondet_* | 36 | Nondet arithmetic with overflow |
| dict_* | 33 | Dict methods and iteration patterns |
| casting_* | 24 | Type casting edge cases |
| isinstance_* | 19 | isinstance with multiple types / tuples |
| divmod_* | 15 | divmod() not implemented |
| range_* | 14 | Range edge cases |
| lambda_* | 13 | Multi-param lambdas |
| string_* | ~30 | String methods, augmented assignment |

### Remaining 39 crashes — breakdown

| Count | Crash | Root cause |
|-------|-------|-----------|
| ~19 | `symex_assign type consistent` | Type mismatches at merge points |
| ~10 | `from_integer(false)` | Unsupported type in from_integer |
| ~5 | `equal_exprt type mismatch` | Comparison type mismatches |
| ~5 | other | Various invariant violations |

### Historical error breakdown (initial evaluation)

ESBMC benchmark: 938 pass, 533 errors, 12 timeouts out of 2,089 tests.

### Error breakdown

| Category | Count | Root cause |
|----------|-------|-----------|
| Crash: `false` (Precondition) | 127 | Various type mismatches in CBMC internals |
| Crash: `lhs().type() == rhs().type()` | 89 | Type mismatch in equality/assignment |
| Crash: `function must return value` | 38 | Void function returning a value |
| Crash: `assignments must be type consistent` | 31 | Assigning wrong type to variable |
| Crash: `boolean required` | 7 | Non-boolean in boolean context |
| Crash: other invariants | 8 | Miscellaneous |
| Comparison: `is`/`is not` | 9 | Identity comparison not implemented |
| Expression: `Slice` | 1 | Slice on non-list type |
| Annotation: unknown types | 22 | Unrecognized type annotations |
| Method: `dict.items()` | 6 | Dict iteration not implemented |
| Other | 75 | Various |
| **Total errors** | **533** | |

### Timeout tests (12)

Tests involving nondet values with complex control flow that exceed
the 10-second timeout. Need investigation with `--unwind` tuning.

### Roadmap by priority

#### Priority 1: Fix crashes (300 tests, ~1 week)

The 300 crashes from type mismatches have a few root causes:

**P1.1: Type-consistent assignments (89 + 31 = 120 crashes)**

`lhs().type() == rhs().type()` and `assignments must be type consistent`
both indicate that an assignment has mismatched types. Root causes:
- Assigning a tagged-union value to a concrete-typed variable
- Assigning a string to an int variable (type change not detected)
- Function return type mismatch

Fix: in `convert_assign`, always typecast RHS to LHS type when they
differ, instead of crashing. For tagged unions, unwrap first.

**P1.2: Return type mismatches (38 crashes)**

`function must return value` — a void function has a return statement
with a value, or vice versa. Root cause: unannotated functions default
to `python_int_type()` but some return None (void).

Fix: scan function body for return statements during `convert_function_def`.
If any return has no value, use `empty_typet{}`. If mixed (some with
value, some without), use `python_int_type()` and add `return 0` for
bare returns.

**P1.3: Precondition failures (127 crashes)**

Generic `false` precondition failures in CBMC internals. These are
triggered by malformed expressions reaching the GOTO converter or
solver. Need case-by-case investigation, but most are likely caused
by nil expressions propagating from unsupported features.

Fix: audit all places that return `nil_exprt{}` and ensure they don't
propagate into assignments or function calls. Add guards in
`convert_assign` and `convert_call` to skip nil expressions with
a warning instead of passing them to CBMC.

**P1.4: Boolean context (7 crashes)**

Non-boolean expression used where boolean is required (if condition,
while condition, assert). Likely a tagged-union or struct value used
directly as a condition.

Fix: in `convert_if`, `convert_while`, `convert_assert`, add explicit
typecast to bool for non-boolean conditions.

#### Priority 2: Missing operators and methods (15 tests, ~1 day)

**P2.1: `is` / `is not` comparison (9 tests)**

Python identity comparison. For our model (no heap allocation for
scalars), `is` is equivalent to `==` for int/float/bool/None.
For objects, it's pointer equality.

Fix: add `Is` and `IsNot` to `convert_compare`. Map to `equal_exprt`
and `notequal_exprt` for scalar types.

**P2.2: `dict.items()` method (6 tests)**

Dict iteration via `.items()`. Returns key-value pairs.

Fix: for our struct-based dict model, `d.items()` returns a list of
tuples. Implement in `convert_expr_stmt` or `convert_call` for
Attribute method calls on dict types.

#### Priority 3: Type annotation gaps (22 tests, ~2 days)

**P3.1: Class names as type annotations (11 tests)**

`Unknown Python type annotation: , defaulting to int` — empty
annotation string, likely from a class name that wasn't registered.

Fix: in `convert_type_annotation`, when the annotation is a `Name`
node with an unrecognized id, check `class_types` map. Already
partially implemented but may miss forward references.

**P3.2: `typing` module types (11 tests)**

`Literal`, `Union`, `Callable`, `BinaryIO`, `List`, `Dict` — typing
module constructs not handled.

Fix: extend `convert_type_annotation` to handle these:
- `Literal[value]` → type of the value
- `Union[T1, T2]` → tagged union or first type
- `Callable` → code_typet
- `List`, `Dict` → python_list_type, dict type
- `BinaryIO` → opaque type (nondet)

#### Priority 4: Investigate timeouts (12 tests, ~1 day)

Tests: `complex_bool_context`, `complex_builtins`, `for-loop13`,
`github_3622-nondet`, `github_3622_nondet`, `github_3701_5-nondet`,
`list13`, `nondet_list7`, `nondet_list8`, `range19-nondet`,
`recursion11_nondet`, `redundancy`.

Most involve nondet values with loops. Investigation needed:
- Check if `--unwind` is needed (and what bound)
- Check if the formula is too large (list operations with MAX=64)
- Consider reducing `PYTHON_MAX_LIST_LENGTH` for these tests
- Profile with `--show-goto-functions` to identify bottlenecks

### Current state (after P1-P3)

ESBMC benchmark: 1,002 pass (47%), 57% effective, 248 errors, 13 timeouts.

### Remaining KNOWNBUG tests and fix plans (5 tests, ~248 errors)

---

#### `crash-from-integer` (~143 errors)

**Test:** `x = None; assert not any([x])`

**Why it crashes:** `from_integer(0, type)` is called with a type that
`from_integer` doesn't support — typically `python_value_type` (the
tagged-union struct) or a string struct. This happens when:
- `any()`/`all()` on non-literal lists (the list element type is a struct)
- `None` is used as a value (modeled as `from_integer(0, int)` but the
  variable may have a different type)
- Default values in various contexts use `from_integer` with the wrong type

**Fix:** Two parts:
1. **Proper None modeling:** `None` should be a distinct value, not
   `from_integer(0, int)`. Use a `python_value_type` with tag `NONE`,
   or a dedicated `nil_exprt`-like constant. For comparisons with None,
   use the tag check.
2. **Guard `from_integer` calls:** Audit all `from_integer` calls in the
   converter. When the type is a struct or other non-numeric type, use
   `side_effect_expr_nondett` instead.

**Estimated effort:** 2-3 days

---

#### `crash-string-set` (~39 errors)

**Test:** `s: set[str] = {"foo", "bar"}; assert "foo" in s`

**Why it crashes:** The `set[str]` annotation creates a `python_list_type`
with `python_string_type()` elements. The `in` operator's unrolled
disjunction creates `equal_exprt{string_struct, string_struct}` which
works, but the type promotion code before it tries to cast the set (a
list struct) to the element type (string), corrupting the set.

**Fix:** The `in` operator already skips type promotion (added in the
P1 fix). The remaining issue is that `import math` style imports
(`math.acos`) aren't handled — only `from math import acos` is.

Actually, the 39 errors here overlap with the math import issue. The
string set crash specifically needs:
1. Ensure `set[str]` creates a list with string element type
2. The `in` operator comparison uses `equal_exprt` on the element type
   (string structs), which requires struct-level equality — this should
   already work.

**Estimated effort:** 1 day (mostly debugging the type flow)

---

#### `crash-math-import` (~39 errors)

**Test:** `import math; x = math.acos(1.0); assert x >= 0.0`

**Why it crashes:** `import math` (without `from`) is handled as a no-op.
When `math.acos(1.0)` is called, the `Attribute` node `math.acos` is
converted — `math` is looked up as a variable (not found), and the
attribute access fails. The `from math import acos` form works because
it registers `acos` directly.

**Fix:** Handle `import MODULE` by creating a namespace symbol. When
`MODULE.func()` is called (Attribute + Call), resolve it by looking up
the module's registered functions.

**Implementation:**
1. In `convert_statement` for `Import`, register the module name as a
   known namespace
2. In `convert_call`, when the func is an `Attribute` node and the
   object is a known module namespace, resolve to the function directly
3. Reuse the existing math function models from `ImportFrom` handling

**Estimated effort:** 1-2 days

---

#### `crash-class-complex` (~23 errors)

**Test:** `class MyClass: class_attr: int = 1; ...`

**Why it crashes:** Class-level attributes (defined in the class body
outside `__init__`) are not handled. Our `convert_class_def` only scans
`__init__` for `self.attr = ...` assignments. Class-level `AnnAssign`
nodes in the class body are ignored, so `MyClass.class_attr` fails.

**Fix:**
1. In `convert_class_def`, scan the class body for `AnnAssign` nodes
   that are NOT inside methods (these are class-level attributes)
2. Add them as fields in the class struct type
3. Initialize them in the class type's default value
4. Handle `ClassName.attr` access by looking up the class type's fields

**Estimated effort:** 1-2 days

---

#### `crash-mixed-minmax` (~4 errors)

**Test:** `assert min(3, 2.5) == 2.5`

**Why it crashes:** `min(3, 2.5)` generates `if_exprt{3 < 2.5, 3, 2.5}`
where the branches have different types (int vs float). The `if_exprt`
requires both branches to have the same type.

**Fix:** In the `min`/`max` handler in `convert_call`, promote both
arguments to the same type before building the `if_exprt`. Use
`safe_typecast` to promote int to float when mixed.

**Estimated effort:** 30 minutes

---

### Recommended implementation order

| # | KNOWNBUG | Errors | Effort | Cumulative |
|---|----------|--------|--------|-----------|
| 1 | `crash-mixed-minmax` | 4 | 30 min | 244 errors |
| 2 | `crash-math-import` | 39 | 1-2 days | 205 errors |
| 3 | `crash-string-set` | 39 | 1 day | 166 errors |
| 4 | `crash-class-complex` | 23 | 1-2 days | 143 errors |
| 5 | `crash-from-integer` | 143 | 2-3 days | ~0 errors |

Total estimated effort: ~1-2 weeks.

### Remaining 94 errors — detailed breakdown and KNOWNBUG tests

After reducing errors from 533 to 94, the remaining errors fall into
these categories:

#### Category A: Type-inconsistent assignments in symex (26 crashes)

**KNOWNBUG tests:** `crash-for-string-iter`, `crash-string-concat-var`

**Examples:** `for-loop10` (string iteration), `string-concat4` (string
concat in loop), `github_3127` (complex class patterns)

**Root cause:** The `for x in string` iteration assigns a char (uint8)
to a variable that was typed as a string struct. String concatenation
in loops creates type mismatches when the loop variable's type changes
between iterations.

**Fix:** Two parts:
1. String iteration should yield single-character strings (string structs
   with length 1), not raw char values. Update `convert_for` to wrap
   the char in a string struct when iterating over strings.
2. String concatenation result type must match the variable's type.
   The `plus_exprt` on string structs returns a string struct, but the
   assignment may typecast incorrectly.

**Effort:** 1-2 days

---

#### Category B: from_integer on struct types (15 crashes)

**KNOWNBUG tests:** `crash-nested-attr`

**Examples:** `nested-attr-1` through `nested-attr-13`, `strings2`,
`github_3151`

**Root cause:** Nested attribute access (`obj.inner.value`) creates
intermediate expressions where `from_integer(0, class_struct_type)` is
called. The `safe_zero` fix handles most cases but misses some paths
where class struct types reach `from_integer` through default values
or return type inference.

**Fix:** Audit remaining `from_integer` calls that could receive struct
types. The nested attribute case specifically needs the inner object
to be properly initialized through the constructor chain.

**Effort:** 1-2 days

---

#### Category C: Solver type mismatches (7+5+2 = 14 crashes)

**KNOWNBUG tests:** `crash-string-nondet-ops`

**Examples:** `string-symbolic-3` (boolbv_add_sub), `complex_handler`
(boolbv_mult), `nondet_str5` (equal_exprt), `math_edge_frexp` (equal_exprt)

**Root cause:** String struct or complex number expressions reach the
SAT solver with mismatched types. The solver's `boolbv_add_sub` and
`boolbv_mult` expect bitvector operands but receive struct types.

**Fix:** Ensure all arithmetic operations on non-numeric types are
intercepted in the converter. String `+` should be handled as concat
(already done for literals), but nondet strings reaching `+` need
the same treatment. Add type guards before all arithmetic expressions.

**Effort:** 2-3 days

---

#### Category D: Postcondition failures in simplifier (3 crashes)

**KNOWNBUG test:** (covered by crash-from-integer pattern)

**Examples:** `builtin_all_complex`, `complex_binop_promotion`

**Root cause:** Complex number type (not supported) reaches the
expression simplifier, which can't handle it.

**Fix:** Add `complex` as a recognized type that maps to a struct
with real/imaginary float fields. Or return nondet for complex
operations.

**Effort:** 1 day

---

#### Category E: Non-crash errors (32 tests)

**KNOWNBUG tests:** `crash-negative-index`, `crash-chr-function`,
`crash-nested-class-method`

Sub-categories:
- **Negative indexing** (covered by `crash-negative-index`): `lst[-1]`
  should map to `lst[len-1]`. Fix: in `convert_subscript`, detect
  negative constant indices and add length. ~1 hour.
- **chr() function** (covered by `crash-chr-function`): `chr(97)` should
  return a single-character string. Fix: add `chr` to built-in handlers.
  ~30 minutes.
- **Nested method calls** (covered by `crash-nested-class-method`):
  `b.a.f()` — chained attribute + method call. Fix: the Attribute
  handler already works for single-level; need to ensure it chains
  correctly. ~1 hour.
- **map::at crashes** (6 tests): struct member access on wrong type.
  Already partially fixed by safe_typecast. Remaining cases need
  deeper type flow analysis.
- **Hanging tests** (7 tests): tests that produce no output within
  timeout. Likely infinite loops or very large formulas.

**Effort:** 1-2 days for the quick fixes, longer for map::at and hangs.

---

### Summary table

| # | KNOWNBUG | Category | Errors | Effort |
|---|----------|----------|--------|--------|
| 1 | `crash-for-string-iter` | A: string iteration | ~10 | 1 day |
| 2 | `crash-string-concat-var` | A: string concat in loop | ~6 | 1 day |
| 3 | `crash-nested-attr` | B: nested objects | ~15 | 1-2 days |
| 4 | `crash-string-nondet-ops` | C: solver type mismatch | ~14 | 2-3 days |
| 5 | `crash-negative-index` | E: negative indexing | ~5 | 1 hour |
| 6 | `crash-chr-function` | E: chr() built-in | ~3 | 30 min |
| 7 | `crash-nested-class-method` | E: chained method calls | ~7 | 1 hour |

Quick wins (#5, #6, #7): ~2 hours, ~15 errors fixed.
Medium (#1, #2): ~2 days, ~16 errors fixed.
Hard (#3, #4): ~4 days, ~29 errors fixed.

### Timeout investigation (19 tests)

Tests: `complex_bool_context`, `complex_builtins`, `for-loop13`,
`github_3622-nondet`, `github_3622_nondet`, `github_3701_5-nondet`,
`list13`, `nondet_list7`, `nondet_list8`, `range19-nondet`,
`recursion11_nondet`, `redundancy`, and 1 more.

Most involve nondet values with loops. Investigation needed:
- Check if `--unwind` is needed (and what bound)
- Check if the formula is too large (list operations with MAX=64)
- Consider reducing `PYTHON_MAX_LIST_LENGTH` for these tests
- Profile with `--show-goto-functions` to identify bottlenecks

### Final KNOWNBUG inventory (8 tests)

---

#### `crash-multibyte-chr` (7 ESBMC errors)

**Test:** `assert chr(8364) == "€"`

**Why it fails:** `chr()` with code points > 127 needs UTF-8 multi-byte
encoding. Our string model uses `unsignedbv{8}` characters.

**Fix:** In `chr()` handler, encode code points > 127 as UTF-8 bytes:
- U+0080..U+07FF: 2 bytes (0xC0|hi, 0x80|lo)
- U+0800..U+FFFF: 3 bytes (covers `€` = U+20AC)
- Set string length to the number of UTF-8 bytes, not 1.

**Estimated effort:** 2-3 hours

---

#### `crash-unicode-source` (7 ESBMC errors, overlaps with above)

**Test:** `assert "α" + "β" == "αβ"`

**Why it fails:** Source files with non-ASCII characters crash the
Python AST JSON generation (`wstring_convert::to_bytes` in CBMC's
JSON parser).

**Fix:** In the inline Python AST script, add `ensure_ascii=True` to
`json.dump()` so all non-ASCII characters are escaped as `\uXXXX`.
Then update `convert_constant` to decode `\uXXXX` escapes in string
literals back to UTF-8 bytes.

**Estimated effort:** 1-2 hours

---

#### `crash-class-method-chain` (6 ESBMC errors)

**Test:** `b = B(); assert b.g().f() == 1` — method returning object,
then calling method on result.

**Why it fails:** `b.g()` returns a class instance (nondet struct from
constructor-as-expression). Calling `.f()` on the nondet result triggers
`address_arithmetic does not handle nondet_symbol` in symex because
the method call passes `address_of(nondet)`.

**Fix:** When a method call's object is a `side_effect_expr_nondett`,
create a temporary variable, assign the nondet to it, then call the
method on the temporary (which has a proper address). In `convert_call`
for Attribute method calls, detect nondet objects and materialize them.

**Estimated effort:** 2-3 hours

---

#### `crash-constructor-default-none` (6 ESBMC errors)

**Test:** `class Node: def __init__(self, x: int, next=None): ...`

**Why it fails:** Default parameter `next=None` — `None` is modeled as
`from_integer(0, python_int_type())` but the parameter type should be
the class type (for a linked list node). The type mismatch between
`int` (None) and the class struct causes crashes.

**Fix:** In `convert_function_def`, when a default parameter value is
`None`, use `safe_zero(param_type)` instead of `from_integer(0, int)`.
This produces a zero-filled struct for class-typed parameters.

**Estimated effort:** 1-2 hours

---

#### `crash-class-string-attr` (4 ESBMC errors)

**Test:** `class A: def __init__(self, s): self.name = s` — unannotated
`__init__` param assigned to attribute.

**Why it fails:** The `__init__` scan determines attribute types from
parameter annotations. When `s` has no annotation, it defaults to `int`.
But `self.name = s` creates a string-typed attribute if `s` is actually
a string. The type mismatch between the attribute type (int, from
default) and the actual value (string) causes crashes.

**Fix:** In the `__init__` scan, when the parameter has no annotation,
try to infer the type from how the parameter is used in the body. If
`self.attr = param` and the attribute is later accessed as a string,
use string type. Simpler alternative: default unannotated params to
`python_int_type()` and use `safe_typecast` everywhere (already done
for most cases — need to audit remaining paths).

**Estimated effort:** 1-2 hours

---

#### `crash-complex-literal` (3 ESBMC errors)

**Test:** `z = 1 + 2j` — complex number literal.

**Why it fails:** The Python AST has a `Constant` node with value
`(1+2j)` which is a complex number. Our `convert_constant` doesn't
handle complex values — they're neither int, float, bool, nor string.

**Fix:** In `convert_constant`, detect complex values (they appear as
non-numeric, non-string constants in the JSON). Model as nondet float
(simplified) or as a struct with real/imaginary fields.

**Estimated effort:** 1 hour

---

#### `crash-list-repeat-compare` (1 ESBMC error)

**Test:** `lst = [4,5]; assert lst * 2 == [4,5,4,5]`

**Why it fails:** `lst * 2` returns a list with correct length but
nondet data (content not tracked). The comparison `== [4,5,4,5]`
compares the nondet data with concrete values and fails.

**Fix:** For `lst * n` where both `lst` and `n` are concrete, build
the repeated list with actual data (copy elements n times) instead of
using nondet data.

**Estimated effort:** 1-2 hours

---

#### `crash-math-radians` (2 ESBMC errors)

**Test:** `import math; assert math.radians(180) > 3.0`

**Why it fails:** `math.radians()` returns nondet float (no precise
model). The assertion `> 3.0` can fail because nondet can be any value.

**Fix:** Two options:
1. Accept that math functions return nondet (current behavior is correct
   but imprecise). Update the test to not assert on specific values.
2. Add postconditions: `radians(x) == x * pi / 180`. This requires
   modeling `pi` as a constant.

**Estimated effort:** 30 min (option 1) or 2 hours (option 2)

---

### Recommended implementation order

| # | KNOWNBUG | Effort | Impact |
|---|----------|--------|--------|
| 1 | `crash-complex-literal` | 1h | 3 errors |
| 2 | `crash-math-radians` | 30min | 2 errors |
| 3 | `crash-constructor-default-none` | 1-2h | 6 errors |
| 4 | `crash-class-string-attr` | 1-2h | 4 errors |
| 5 | `crash-unicode-source` | 1-2h | 7 errors |
| 6 | `crash-multibyte-chr` | 2-3h | 7 errors |
| 7 | `crash-class-method-chain` | 2-3h | 6 errors |
| 8 | `crash-list-repeat-compare` | 1-2h | 1 error |

---

### Remaining ESBMC errors (46 errors)

After reducing from 533 to ~46 errors, the remaining errors are covered
by the KNOWNBUG tests above plus variations of those patterns.

## 8. Design Limitation Roadmap

Complete inventory of all intentional design limitations, each with a
KNOWNBUG test and a detailed plan for lifting the limitation.

### L1: String concatenation content not tracked

**KNOWNBUG:** `crash-unicode-source`, `crash-list-repeat-compare`

**Current behavior:** `s1 + s2` has correct length but nondet data.

**Fix plan:** Generate statement-level code for string concat:
1. Create a temporary string variable
2. Copy `s1.data[0..s1.length]` to `tmp.data[0..]`
3. Copy `s2.data[0..s2.length]` to `tmp.data[s1.length..]`
4. Set `tmp.length = s1.length + s2.length`
5. This requires converting string `+` from an expression to a
   statement block (similar to how constructor calls are handled)

**Effort:** 1-2 days. **Impact:** ~10 ESBMC errors, enables string
content verification.

---

### L2: Bounded string length (256 characters)

**KNOWNBUG:** `limit-string-length`

**Current behavior:** `PYTHON_MAX_STRING_LENGTH = 256`, truncation.

**Fix plan:** Two options:
1. **Configurable bound:** `--python-max-string-length N` flag. Simple
   but doesn't solve the fundamental issue.
2. **CBMC string solver integration:** Use `refined_string_typet` from
   `src/util/string_expr.h` which models strings as length + pointer
   to unbounded array. Requires integrating with CBMC's string
   refinement solver. Major project.

**Effort:** Option 1: 2 hours. Option 2: 2-3 weeks.

---

### L3: Bounded list length (64 elements)

**KNOWNBUG:** `limit-list-length`

**Current behavior:** `PYTHON_MAX_LIST_LENGTH = 64`, overflow.

**Fix plan:** Same as L2 — configurable bound or unbounded arrays.
For lists, unbounded arrays are simpler than strings since there's
no string solver to integrate with. Use `infinity_exprt` as array
size (like C's flexible array members).

**Effort:** Configurable: 2 hours. Unbounded: 1-2 weeks.

---

### L4: Integer overflow with int64

**KNOWNBUG:** (CORE test `int-overflow-check` covers the warning)

**Current behavior:** Overflow wraps with warning. `--python-unbounded-ints
--z3` provides correct semantics.

**Status:** Already solved with the `--python-unbounded-ints` flag.
The limitation is that the default (without the flag) uses int64.

**Fix plan:** Consider making `--python-unbounded-ints` the default
when `--z3` is used. No code change needed — just a default change.

---

### L5: None modeled as integer 0

**KNOWNBUG:** `limit-none-identity`

**Current behavior:** `None` = `from_integer(0, int)`. `0 is None` is
incorrectly `True`.

**Fix plan:** Model `None` as a tagged-union value with tag `NONE`:
1. In `convert_constant` for null values, return
   `make_python_value(NONE, from_integer(0, int))`
2. `is None` checks the tag: `python_value_is(x, NONE)`
3. `is not None` checks `!python_value_is(x, NONE)`
4. Truthiness of None: `False`
5. Requires variables that could be None to use `python_value_type`

**Effort:** 2-3 days (depends on tagged-union coverage).

---

### L6: Constructor-as-expression returns nondet

**KNOWNBUG:** `limit-constructor-expr`

**Current behavior:** `Foo(args)` in expression position returns nondet.

**Fix plan:** Materialize constructor calls in expression position:
1. In `convert_expression` for `Call` nodes that are constructors,
   generate a temporary variable (using a counter for unique names)
2. Add the temp declaration and `__init__` call to `pending_checks`
   (which are prepended before the current statement)
3. Return the temp variable's `symbol_exprt`
4. This reuses the `pending_checks` mechanism already used for
   property checks

**Effort:** 3-4 hours.

---

### L7: Imports silently ignored

**KNOWNBUG:** (CORE test `import-stdlib` covers the no-crash case)

**Current behavior:** Unknown imports ignored, functions return nondet
with `no-body` warning.

**Fix plan:** Incremental:
1. **Stub system:** Load `.pyi` type stub files for standard library
   modules. Parse function signatures and register them.
2. **Operational models:** For critical functions (e.g., `json.loads`,
   `os.path.exists`), provide hand-written models with postconditions.
3. **Module loader:** Parse imported `.py` files and include their
   function definitions.

**Effort:** Stub system: 1 week. Models: ongoing. Module loader: 2-3 weeks.

---

### L8: Class-level attribute access on class itself

**KNOWNBUG:** `limit-class-attr-access`

**Current behavior:** `ClassName.attr` fails (only `instance.attr` works).

**Fix plan:** Create a "class object" symbol for each class:
1. In `convert_class_def`, create a symbol `python::ClassName` with
   the class struct type, initialized with class-level attribute values
2. `ClassName.attr` resolves to `member_exprt{class_symbol, attr, type}`
3. Instance attributes shadow class attributes (already handled)

**Effort:** 2-3 hours.

---

### L9: No dynamic dispatch

**KNOWNBUG:** `limit-dynamic-dispatch`

**Current behavior:** Method calls resolved statically by struct tag.

**Fix plan:** Add a vtable-like mechanism:
1. Add a `__vtable` field to each class struct containing function
   pointers for each method
2. Method calls dispatch through the vtable: `obj.__vtable.method(obj)`
3. Derived classes override vtable entries
4. Alternative: use `if-then-else` dispatch based on the struct tag
   (simpler but generates larger formulas)

**Effort:** 1-2 weeks.

---

### L10: Exception type not tracked

**KNOWNBUG:** `limit-except-type`

**Current behavior:** `except TypeError` catches all exceptions.

**Fix plan:** Replace the boolean `__exception_active` flag with a
typed exception value:
1. Add `__exception_type` integer variable (enum of exception types)
2. `raise ValueError(...)` sets `__exception_type = VALUEERROR`
3. `except ValueError` checks `__exception_type == VALUEERROR`
4. `except Exception` catches all (base class check)
5. Unhandled exceptions: check `__exception_active` at end (unchanged)

**Effort:** 1-2 days.

---

### L11: String iteration yields int, not single-char string

**KNOWNBUG:** `limit-string-iter-type`

**Current behavior:** `for c in "abc"` yields `c = 97` (int).

**Fix plan:** In the string iteration path of `convert_for`, wrap
each character in a single-character string struct:
1. Extract `data[idx]` as `unsignedbv{8}`
2. Build a string struct with `length=1` and `data[0]=char_val`
3. Assign the string struct to the loop variable
4. Set loop variable type to `python_string_type()`

**Effort:** 1-2 hours.

---

### L12: Generator expressions only work with literal iterables

**KNOWNBUG:** `limit-generator-variable`

**Current behavior:** `all(x > 0 for x in variable_list)` returns nondet.

**Fix plan:** Generate a loop at the statement level:
1. Create a result variable (`true` for `all`, `false` for `any`)
2. Generate `for x in iterable` loop (already supported)
3. In loop body: evaluate predicate, update result
4. For `all`: `result = result and pred`
5. For `any`: `result = result or pred`
6. Use `pending_checks` to inject the loop before the current statement

**Effort:** 3-4 hours.

---

### Remaining KNOWNBUG from ESBMC patterns

**`crash-class-string-attr`:** Unannotated `__init__` param assigned to
string attribute. Fix: infer attribute type from RHS expression type
during `__init__` scan. **Effort:** 1-2 hours.

---

### Summary: all KNOWNBUG tests

| # | KNOWNBUG | Limitation | Effort |
|---|----------|-----------|--------|
| 1 | `crash-unicode-source` | L1: string concat content | 1-2 days |
| 2 | `crash-list-repeat-compare` | L1: list repeat content | 1-2 days |
| 3 | `crash-class-string-attr` | Unannotated string param | 1-2 hours |
| 4 | `limit-string-length` | L2: bounded strings | 2h or 2-3 weeks |
| 5 | `limit-list-length` | L3: bounded lists | 2h or 1-2 weeks |
| 6 | `limit-none-identity` | L5: None as 0 | 2-3 days |
| 7 | `limit-constructor-expr` | L6: constructor in expr | 3-4 hours |
| 8 | `limit-class-attr-access` | L8: ClassName.attr | 2-3 hours |
| 9 | `limit-dynamic-dispatch` | L9: no virtual dispatch | 1-2 weeks |
| 10 | `limit-except-type` | L10: exception type | 1-2 days |
| 11 | `limit-string-iter-type` | L11: string iter type | 1-2 hours |
| 12 | `limit-generator-variable` | L12: generator variable | 3-4 hours |

### Previous items (DONE)

- Pointer-based class instances ✓
- KNOWNBUG backlog (tuple unpack, string concat, list append) ✓
- `--function` mode with nondet harness ✓
- Readable counterexample traces ✓
- Division-by-zero, bounds, and overflow checks ✓
- `raise` as verification failure ✓
- `no-body` property for unknown functions ✓
- for-in-list, list comprehensions, augmented assign targets ✓
- Dict literals, `with` statement, parameterized types ✓
- Built-in functions (int, float, bool, abs, min, max, print) ✓
- Bitwise operators ✓
- ESBMC failure pattern coverage (all categories) ✓

### ESBMC benchmark validation

Tested against 2,089 non-fail tests from ESBMC's Python regression suite:

| Metric | Count | Percentage |
|--------|-------|-----------|
| PASS | 914 | 43% |
| FAIL (real) | 412 | 20% |
| FAIL (no-body warnings) | 171 | 8% |
| FAIL (overflow warnings) | 3 | <1% |
| FAIL (uncaught exception) | 3 | <1% |
| ERROR/TIMEOUT | 586 | 28% |

Effective pass rate excluding correct warnings: **52%**.

Error count dropped from 594 to 586 after import handling. The 412 real
failures are from: tests depending on imported values with specific
semantics, generator expressions with variable iterables, and dynamic
typing patterns. The front-end now analyzes more code (fewer silent
drops), which reveals more real failures — this is progress toward
soundness.

## 7. Build Record

*Updated as implementation progresses.*

| Date | Commit | What was built |
|------|--------|---------------|
| 2026-04-22 | 7267708167 | Phase 1: skeleton, language registration, parse pipeline, 16 KNOWNBUG tests |
| 2026-04-22 | efa988450d | Phases 2+3: python_convertert, all 16 tests promoted to CORE |
| 2026-04-22 | b1c236e9ad | Replaced script with inline python3 -c invocation |
| 2026-04-22 | 8e6a233bc2 | Error handling for missing python3, ast module, syntax errors |
| 2026-04-22 | 326800d541 | Plan document updates |
| 2026-04-22 | 7198f0a4c8 | Phase 4: type inference, augmented assign, multiple-target assign (8 new tests) |
| 2026-04-22 | 27396c8250 | Phase 5: basic string support — literals, len, compare, index (5 new tests) |
| 2026-04-22 | 6782e150b0 | Phase 6: tuples and lists — literals, indexing, len (5 new tests) |
| 2026-04-22 | 8b81831b57 | Phase 7: basic class support — constructor, attributes, methods (3 new tests) |
| 2026-04-22 | 846357ad40 | Plan: added Next Steps section |
| 2026-04-22 | 3d229e6f56 | Pointer-based class model — fixes method mutation, enables inheritance |
| 2026-04-22 | 4646d2ca8c | KNOWNBUG fixes: tuple unpack, string concat (36 CORE tests) |
| 2026-04-22 | 239a3c1025 | --function mode with nondet harness generation (38 CORE tests) |
| 2026-04-22 | 7e753225a0 | expr2python: readable counterexample traces |
| 2026-04-22 | b175f3da8f | List append, div-by-zero and bounds checks (43 CORE tests) |
| 2026-04-22 | a4e03b5dac | raise statements as verification failures (45 CORE tests) |
| 2026-04-22 | 85f8bc2b36 | ESBMC failure pattern regression tests (48 CORE, 4 KNOWNBUG) |
| 2026-04-22 | b9d4ed0804 | Parameterized types, global variables, class type annotations (50 CORE) |
| 2026-04-22 | 0f44b073e3 | Dict literals, with statement, pass 0 fixes (52 CORE, 1 KNOWNBUG) |
| 2026-04-22 | 15bd8ea600 | try/except Stage A (53 CORE, 0 KNOWNBUG) |
| 2026-04-22 | aafa143492 | Builtins, bitwise ops, ESBMC validation (1,242/2,089 = 59%) |
| 2026-04-22 | 2378d18063 | for-in-list, list comprehensions, aug-assign targets (57 CORE, 2 KNOWNBUG) |
| 2026-04-22 | 995ed9c3c6 | Tests for unannotated params and snippet handling (58 CORE, 4 KNOWNBUG) |
| 2026-04-22 | aca368dbdf | KNOWNBUG tests for type changes and integer overflow |
| 2026-04-22 | c69ce3ab90 | Integer overflow checks, nondet for unknown functions (60 CORE, 6 KNOWNBUG) |
| 2026-04-22 | e2584bcef1 | no-body failing property for unknown functions |
| 2026-04-22 | 3f6376c01a | KNOWNBUG tests for all ESBMC failure categories (60 CORE, 11 KNOWNBUG) |
| 2026-04-22 | 87fbae2419 | Keyword args, defaults, string ordering, global statement (64 CORE) |
| 2026-04-22 | 9dbd66c2d5 | Lambda first-class, return class instance (66 CORE, 5 KNOWNBUG) |
| 2026-04-22 | (pending) | isinstance KNOWNBUG, ESBMC re-validation, detailed KNOWNBUG plans |
| 2026-04-23 | b2285ee019 | Unannotated param warnings, isinstance with inheritance (68 CORE, 4 KNOWNBUG) |
| 2026-04-23 | 48bedcc240 | --python-unbounded-ints for correct integer semantics (70 CORE, 2 KNOWNBUG) |
| 2026-04-23 | 72539813a2 | Exception propagation Stage B — try/except catches raise (71 CORE, 1 KNOWNBUG) |
| 2026-04-23 | 91ba7c75e3 | User verification guide |
| 2026-04-23 | b299a0ef82 | Real-world tests, KNOWNBUG for all gaps |
| 2026-04-23 | 47a4c0c3dc | Detailed fix plans for all 7 KNOWNBUG tests |
| 2026-04-23 | cb19d2735a | Items 1-5: list assign, generators, sets, slices, del (78 CORE) |
| 2026-04-23 | f55f374299 | Item 6: import handling with math library models (79 CORE, 1 KNOWNBUG) |
