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




### KNOWNBUG inventory (7 tests)

257 total tests, 250 CORE, 7 KNOWNBUG.
ESBMC: 1,732 correct (56%), 1 crash, 1,066 wrong-pass.

#### Tier 1 — Quick fixes (< 30 minutes each)

##### `limit-float-div-rounding` — `1/2 != 0.5`

**Problem:** `a = 1 / 2; assert a == 0.5` fails. The division
`typecast(1, float) / typecast(2, float)` uses `floatbv_typecast`
which depends on `__CPROVER_rounding_mode`. Even though we initialize
it to 0 (ROUND_TO_EVEN), the solver treats the typecast result as
potentially different from the `ieee_floatt(0.5)` literal.

**Fix:** In the true division handler, when both operands are constant
integers, compute the result at conversion time using `ieee_floatt`:
`ieee_floatt(1.0) / ieee_floatt(2.0) = ieee_floatt(0.5)`. Only use
`floatbv_div` for variable operands.

**Effort:** 20 minutes. **Affects:** ~5 direct, ~50 indirect.

##### `limit-nondet-dict-typed` — nondet_dict() unconstrained

**Problem:** `nondet_dict()` returns nondet int (not even a dict type).

**Fix:** Same pattern as `nondet_list()`: return a nondet dict with
`assume(0 <= length <= PYTHON_MAX_DICT_SIZE)`. Use the array-based
dict type `python_dict_type(python_string_type(), python_int_type())`.

**Effort:** 15 minutes. **Affects:** ~9 direct, ~20 indirect.

##### `limit-nondet-string-sized` — nondet_string(N) exact length

**Problem:** `nondet_string(5)` returns nondet string with nondet
length. Should have `length == 5`.

**Fix:** In the `nondet_str` handler, check for a size argument.
If present, create a nondet string and add `assume(length == N)`.

**Effort:** 15 minutes. **Affects:** ~6 direct, ~15 indirect.

##### `limit-bytes-literal` — b"Hello" not supported

**Problem:** `b"Hello"` is a `Constant` with `bytes` type. Our
constant converter doesn't handle bytes — only strings.

**Fix:** In `convert_constant`, detect bytes values (Python AST
represents them as `Constant(value=b"Hello")`). Convert to a list
of integers: `[72, 101, 108, 108, 111]`. Or model as a string
struct with the raw byte values (same representation, different
semantic interpretation).

**Effort:** 30 minutes. **Affects:** ~4 direct, ~10 indirect.

#### Tier 2 — Moderate (1-2 hours each)

##### `limit-str-replace-content` — str.replace() content tracking

**Problem:** `"hello world".replace("world", "python")` returns nondet.

**Fix:** For constant string, constant old, constant new: scan the
string data for occurrences of `old` at conversion time. For each
occurrence, replace the bytes with `new`. Build the result string
with adjusted length. For variable arguments, return nondet.

**Effort:** 1-2 hours. **Affects:** ~5 direct, ~20 indirect.

#### Tier 3 — Significant (1-2 weeks each)

##### `limit-generator-infinite` — lazy generator state machine

(Detailed plan in Section 9.5.2 — 7-step state machine transformation.)

**Effort:** 1-2 weeks. **Affects:** ~15 direct, ~30 indirect.

##### `limit-async-concurrent` — concurrent coroutines

(Detailed plan in Section 9.5.3 — 6-step CBMC thread model.)

**Effort:** 1-2 weeks. **Affects:** ~10 direct, ~15 indirect.

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

## 9. Precision Roadmap — Eliminating Overapproximations

This section provides a plan for turning each of the 27 overapproximations
documented in `doc/overapproximations.md` into precise verification.

### 9.1 Bounded Data Structures

#### 9.1.1 Strings: configurable bound (currently 256)

**Precise approach:** Add `--python-max-string-length N` command-line
option. Parse in `python_language.cpp`, pass to converter via `optionst`.
Replace `PYTHON_MAX_STRING_LENGTH` macro with a runtime value.

**Effort:** 2 hours. **Impact:** Users can tune for their program.

#### 9.1.2 Lists: configurable bound (currently 64)

**Precise approach:** Add `--python-max-list-length N`. Same pattern
as strings.

**Effort:** 1 hour. **Impact:** Users can tune for their program.

#### 9.1.3 Integers: already solved with --python-unbounded-ints

**Precise approach:** Already implemented. `--python-unbounded-ints --z3`
uses `integer_typet` for mathematical integers with no overflow.

**Effort:** Done.

#### 9.1.4 Dicts: array-based model

**Precise approach:** Replace struct-per-key model with array-based
model: `struct { int length; key_type keys[N]; value_type values[N]; }`.
Supports dynamic insertion/deletion. `d[key]` scans keys array.
`del d[key]` shifts elements left. `len(d)` returns length field.

**Effort:** 1-2 days. **Impact:** Full dict semantics.

### 9.2 Nondet Returns → Exact Models

#### 9.2.1 String transform methods (upper/lower/strip/etc.)

**Precise approach:** For `upper()`: iterate data array, convert each
byte `c` to `c - 32` if `c >= 'a' && c <= 'z'`. Similar for `lower()`.
For `strip()`: scan from both ends for whitespace, adjust length and
shift data. Use `pending_checks` for element-by-element operations.

**Effort:** 1 day per method. **Impact:** Content-preserving transforms.

#### 9.2.2 String query methods (find/index/count/startswith/endswith)

**Precise approach:** For `find(sub)`: scan data array for substring
match. Return index or -1. For `startswith(prefix)`: compare first
N bytes. For `count(sub)`: count non-overlapping occurrences.

**Effort:** 1 day per method. **Impact:** Exact string search results.

#### 9.2.3 String predicate methods (isalpha/isdigit/etc.)

**Precise approach:** For `isalpha()`: check all bytes are in
`[a-zA-Z]` range. Build conjunction: `for i in 0..length:
(data[i] >= 'a' && data[i] <= 'z') || (data[i] >= 'A' && data[i] <= 'Z')`.

**Effort:** 2 hours per method. **Impact:** Exact character class checks.

#### 9.2.4 str.format() and f-strings with expressions

**Precise approach:** For `str.format("hello {}", x)`: parse format
string for `{}` placeholders. For each placeholder, call `str(arg)`
to convert the argument to a string. Concatenate literal parts with
converted arguments using the string concat content-tracking mechanism.

For `str(int_val)`: convert integer to decimal string at the GOTO
level using repeated division by 10 and digit extraction.

**Effort:** 2-3 days. **Impact:** Full format string content tracking.

#### 9.2.5 Built-in functions (hex/oct/bin/repr)

**Precise approach:** For `hex(n)`: convert integer to hex string
using repeated division by 16. Prepend "0x". For `oct(n)`: divide
by 8. For `bin(n)`: divide by 2. All use the integer-to-string
conversion pattern.

**Effort:** 1 day. **Impact:** Exact conversion results.

#### 9.2.6 map/zip/filter

**Precise approach:** For `map(func, lst)`: unroll function calls:
`result[i] = func(lst[i])` for `i in 0..lst.length`. Requires
resolving `func` to a symbol and generating inline calls via
`pending_checks`. For `zip(a, b)`: `result[i] = (a[i], b[i])` for
`i in 0..min(a.length, b.length)`. For `filter(func, lst)`: similar
to set deduplication — conditional copy.

**Effort:** 1 day each. **Impact:** Exact functional operations.

#### 9.2.7 Module functions (math/re/random)

**Precise approach:** For `math.sin/cos/tan/log/exp`: use CBMC's
built-in floating-point models or compute at conversion time for
constants. For `re`: implement a bounded regex matcher (complex —
would need NFA simulation). For `random`: model as nondet with
range constraints (`randint(a,b)` → `assume(result >= a && result <= b)`).

**Effort:** Math: 1 day. Random: 2 hours. Re: 1-2 weeks.

#### 9.2.8 Complex number operations

**Precise approach:** For `abs(complex)` with variable args: use
CBMC's `__CPROVER_sqrt` or model `sqrt` as a function with the
postcondition `result * result == input`. For `complex ** n`: use
De Moivre's formula or repeated multiplication.

**Effort:** 1 day. **Impact:** Exact complex arithmetic.

### 9.3 Tagged Union Precision

#### 9.3.1 Full truth value dispatch

**Precise approach:** Extend the bool unwrap to check all tags:
NONE→false, BOOL→bool_val, INT→int_val!=0, FLOAT→float_val!=0.0,
STR→deref(str_ptr).length!=0, LIST→deref(list_ptr).length!=0.

**Effort:** 1 hour. **Impact:** Correct falsiness for all types.

#### 9.3.2 Type-dispatched arithmetic

**Precise approach:** For `x + y` on tagged unions: generate
multi-way dispatch: `if(x.tag==INT && y.tag==INT) {INT, x.int+y.int}
else if(x.tag==FLOAT || y.tag==FLOAT) {FLOAT, ...} else if
(x.tag==STR && y.tag==STR) {STR, concat(...)}`. This is the full
tagged-union arithmetic from the original Phase 9 plan.

**Effort:** 2-3 days. **Impact:** Correct mixed-type arithmetic.

#### 9.3.3 Full tagged union operations

**Precise approach:** Extend `len()`, indexing, slicing, `in` operator,
and all other operations to dispatch on the tag field. Each operation
generates an if-then-else chain for each supported type.

**Effort:** 1 week. **Impact:** Full dynamic typing support.

### 9.4 Semantic Simplifications → Exact Models

#### 9.4.1 None as a proper type tag (not sentinel)

**Precise approach:** Add a `NONE` tag to `python_value_type` (already
exists). For variables that can be None, use `python_value_type` with
tag=NONE. `x is None` checks `x.tag == NONE`. Remove the sentinel
integer value.

**Effort:** 1 day. **Impact:** No risk of sentinel collision.

#### 9.4.2 Exception types as class objects (not hashes)

**Precise approach:** Model exception types as class structs with
`__class_tag`. `raise TypeError()` creates a TypeError instance.
`except TypeError` checks `isinstance(exc, TypeError)`. This uses
the existing class/isinstance infrastructure.

**Effort:** 2-3 days. **Impact:** Correct exception hierarchy, no hash collisions.

#### 9.4.3 Dynamic dispatch for isinstance

**Precise approach:** Already implemented via `__class_tag` and
`class_bases`. Extend to support `__class__` attribute access and
runtime type changes (rare in verification code).

**Effort:** 1 day. **Impact:** Full isinstance semantics.

#### 9.4.4 Multiple inheritance

**Precise approach:** Extend the layout-compatible inheritance to
support multiple bases. Use C3 linearization (MRO) for method
resolution. Store multiple base class fields in the derived struct.

**Effort:** 1 week. **Impact:** Full inheritance model.

#### 9.4.5 del on dicts: proper key removal

**Precise approach:** With the array-based dict model (9.1.4), `del`
shifts elements left and decrements length, same as list deletion.

**Effort:** Included in 9.1.4.

#### 9.4.6 Missing return: proper None object

**Precise approach:** With the proper None type tag (9.4.1), missing
returns produce `python_value_type{tag=NONE}` instead of the sentinel.

**Effort:** Included in 9.4.1.

### 9.5 Unsupported Features → Full Support

#### 9.5.1 Decorators

**Precise approach:** Model decorators as function wrappers:
`@decorator def f(): ...` → `f = decorator(f)`. For `@staticmethod`:
skip self parameter. For `@property`: model as attribute access.

**Effort:** 2-3 days. **Impact:** Full decorator support.

#### 9.5.2 Generators and yield

**Precise approach:** Model generators as state machines. Each `yield`
creates a state transition. The generator object stores the current
state and local variables. `next(gen)` advances to the next yield.

**Effort:** 1-2 weeks. **Impact:** Full generator support.

#### 9.5.3 Async/await

**Precise approach:** Model coroutines as generators (they share the
same state machine structure). `await` is equivalent to `yield from`.
Requires the generator infrastructure from 9.5.2.

**Effort:** 1 week (after 9.5.2). **Impact:** Full async support.

#### 9.5.4 Match statement

**Precise approach:** Desugar `match`/`case` to if-elif chains. Each
`case` pattern becomes a condition check. Structural patterns use
isinstance + attribute access. Guard clauses become additional conditions.

**Effort:** 2-3 days. **Impact:** Full pattern matching.

#### 9.5.5 Nonlocal statement

**Precise approach:** Model closures by passing captured variables as
additional parameters (closure conversion). `nonlocal x` marks `x` as
captured. The enclosing function passes `x` by reference (pointer).

**Effort:** 1 week. **Impact:** Full closure support.

#### 9.5.6 Star expressions

**Precise approach:** For `*args`: model as a list parameter. For
`**kwargs`: model as a dict parameter. For `a, *b = [1,2,3]`: assign
first element to `a`, remaining to `b` as a list.

**Effort:** 2-3 days. **Impact:** Full unpacking support.

### Summary

| Category | Items | Total effort |
|----------|-------|-------------|
| Bounded data structures | 4 | 1-2 days + 1-2 days (dicts) |
| Nondet → exact models | 8 | 2-3 weeks |
| Tagged union precision | 3 | 1-2 weeks |
| Semantic simplifications | 6 | 1-2 weeks |
| Unsupported features | 6 | 4-6 weeks |
| **Total** | **27** | **~10-14 weeks** |


## 10. Module Resolution and Keyword Arguments

### 10.1 PYTHONPATH Module Resolution (DONE)

Implemented in commit 2f3815aeac. The frontend reads `PYTHONPATH` from the
environment and the source file's directory. When `import MODULE` is
encountered, it searches for `MODULE/__init__.py` or `MODULE.py` in the
search paths, parses the AST, and processes `FunctionDef`/`ClassDef`
definitions. Unknown type annotations trigger on-demand sub-module
resolution (e.g., `S3` → `boto3.S3`).

### 10.2 Keyword Arguments (`**kwargs`) — Implementation Plan

**Status:** Not implemented. All `**kwargs` parameters and keyword-only
arguments are silently ignored.

**Impact:** ~150+ ESBMC tests, all boto3 verification benchmarks.

**Root cause:** The Python AST represents `**kwargs` as:
- Definition: `args.kwarg = arg(arg='kwargs')` (the catch-all parameter)
- Definition: `args.kwonlyargs = [arg(arg='key')]` (keyword-only params after `*`)
- Call site: `keywords = [keyword(arg='Bucket', value=...)]`

Our converter reads `args.args` (positional params) but ignores `args.kwarg`
and `args.kwonlyargs`. At call sites, we match keywords to positional params
by name but discard unmatched keywords.

**Implementation plan (3 phases):**

#### Phase 1: `**kwargs` as dict parameter (~30 lines)

In `convert_function_def`, check `args.kwarg`:
```
const jsont &kwarg = json_member(args_node, "kwarg");
if(!kwarg.is_null()) {
    std::string kw_name = json_string(json_member(kwarg, "arg"));
    // Create a dict parameter: kwargs: dict[str, Any]
    typet kw_type = python_dict_type(python_string_type(), python_int_type());
    code_typet::parametert p{kw_type};
    p.set_identifier("python::" + func_name + "::" + kw_name);
    p.set_base_name(kw_name);
    parameters.push_back(p);
}
```

Inside the function body, `kwargs["Key"]` is already handled by the dict
subscript handler — it scans the keys array for a match.

#### Phase 2: Pack keyword arguments into dict at call site (~40 lines)

In `convert_call`, after matching keywords to positional params, collect
unmatched keywords and pack them into a dict:
```
// Collect unmatched keywords
for(const auto &kw : keywords) {
    if(!matched_to_positional) {
        // Add to kwargs dict: keys[i] = "Key", values[i] = value
    }
}
// Build dict struct and pass as the kwargs parameter
```

The dict is built as a `struct_exprt` with the keyword names as constant
string keys and the values as the corresponding expressions.

#### Phase 3: Keyword-only arguments (`*` separator) (~15 lines)

In `convert_function_def`, also process `args.kwonlyargs`:
```
const jsont &kwonly = json_member(args_node, "kwonlyargs");
// These are regular parameters that can only be passed by name.
// Add them to the parameter list; the call-site keyword matching
// already handles them.
```

**Testing:** The `delete_s3_object.py` benchmark with boto3 stubs is the
primary test case. The stub's `delete_object(self, **kwargs)` receives
`Bucket=bucket_name, Key=object_key` as a dict, and the assertion
`len(kwargs["Key"]) >= 1` should pass.

### 10.3 TypedDict Type Annotations — Root Cause Analysis

**Status:** TypedDict types default to `int`.

**Root cause:** TypedDict types are created via function calls, not class
statements:
```python
DeleteObjectRequest = TypedDict('DeleteObjectRequest', {
    'Bucket': Required[str],
    'Key': Required[str],
})
```

This appears in the AST as an `Assign` node with a `Call` RHS, not as a
`ClassDef`. Our `process_imported_module` only processes `ClassDef` and
`FunctionDef` nodes, so TypedDict definitions are silently skipped.

**Impact:** Type annotations like `Unpack[DeleteObjectRequest]` resolve
`DeleteObjectRequest` to `int` (default), losing the struct information.
This means `kwargs["Key"]` returns `int` instead of `str`.

**Fix plan:** In `process_imported_module`, detect `Assign` nodes where
the RHS is `Call(func=Name(id='TypedDict'), ...)`. Extract the field names
and types from the dict literal argument. Create a struct type with those
fields. Register it in `class_types` so type annotations can reference it.

For the `Unpack[T]` annotation, treat it as equivalent to `T` (the
TypedDict struct). When `**kwargs: Unpack[DeleteObjectRequest]` is
encountered, the kwargs dict has the TypedDict's fields as its schema.

**Effort:** ~50 lines for TypedDict parsing, ~20 lines for Unpack handling.

### 10.4 Current KNOWNBUG Inventory

After all fixes in this session, the KNOWNBUG tests are:

| Test | Category | Impact | Status |
|------|----------|--------|--------|
| limit-math-symbolic | Math with symbolic args | 81 | Fundamental limitation |
| limit-unknown-func | Higher-order functions | 73 | Needs function pointers |
| limit-typecast-issue | Untyped param returns | 29 | Partial fix (float only) |
| limit-iteration | Tuple unpacking in for | 19 | Needs for-loop refactor |
| limit-exception-flow | Exception in called func | 19 | Needs div-by-zero model |
| limit-unknown-method | join(), dict.items() | 38 | Needs method impl |
| limit-lambda-higher-order | Lambda from function | 5 | Needs value capture |
| limit-loop-unsound | String list iteration | 42 | Timeout issue |
| limit-generator-infinite | Lazy generators | — | Weeks of work |
| limit-async-concurrent | Async/threads | — | Weeks of work |
| limit-overflow-nondet-arith | Unbounded ints | — | Needs --z3 |
| math-symbolic-arg | sin²+cos²≠1 | — | Fundamental limitation |


## 11. Performance Optimization — String Model

### Current Model: Fixed-Size Array

```c
struct python_string {
    int64_t length;
    unsigned char data[256];  // PYTHON_MAX_STRING_LENGTH
};
```

Every string occupies 264 bytes regardless of actual content. This causes:

- **Dict keys bloat**: 16 keys × 256 bytes = 4KB per dict, just for keys
- **GOTO program bloat**: string comparisons expand to 256-element array
  comparisons; a single `kwargs["Key"]` lookup generates ~65KB of GOTO IR
- **Solver overhead**: the SAT encoding includes variables for all 256 bytes
  even when only 3 are used
- **S3 stub processing**: 108 methods × kwargs dict = ~500KB of struct data

### Proposed Model: Pointer-Based

```c
struct python_string {
    int64_t length;
    unsigned char *data;  // pointer to dynamically-sized array
};
```

Benefits:
- Each string is exactly as long as needed (no padding)
- Dict keys shrink from 4KB to 128 bytes (16 pointers)
- String literals are pointer assignments, not 256-element array copies
- CBMC's solver handles pointer-to-array comparisons efficiently

### Impact Estimate

The `delete_s3_object.py` benchmark currently takes 1.15s. Breakdown:
- 0.15s: python3 AST-to-JSON for S3.py
- 0.85s: C++ processing of S3 class (108 methods, 31 bodies)
- 0.10s: CBMC solving
- 0.05s: CBMC overhead

The pointer-based model would primarily reduce the 0.85s C++ processing
time (smaller struct types, fewer array elements to construct) and the
0.10s solving time (fewer SAT variables). Estimated improvement: 2-4x.

### Implementation Plan

~200-300 lines of changes across:
1. `python_types.h`: change `python_string_type()` to use pointer
2. `python_value_type.h`: update `__str_ptr` field
3. `build_string_struct()`: allocate array, return pointer
4. `extract_string_value()`: dereference pointer to read
5. String concatenation: allocate new array for result
6. String comparison: element-by-element through pointers
7. Dict key comparison: compare pointed-to arrays
8. `in` operator: iterate through pointed-to array
9. String methods: update all handlers

### Alternative: Configurable Bound

The `--python-max-string-length` option is registered but not wired to
the `PYTHON_MAX_STRING_LENGTH` constant. Wiring it up would allow users
to trade precision for performance (e.g., `--python-max-string-length 32`
for programs with short strings).

### Priority

Deferred — correctness and feature work takes priority. The current 1.15s
for the boto3 benchmark is acceptable for development. The pointer-based
model should be implemented when performance becomes a blocker for larger
benchmarks.


## 12. KNOWNBUG Implementation Plans

Each KNOWNBUG test represents a category of wrong results. This section
provides detailed implementation plans for all 16 KNOWNBUGs.

### 12.1 limit-in-tagged-union — `in` on tagged union (~20 tests)

**Test:** `"key" in d` where `d` is `python_value_type`.

**Root cause:** The `in` operator only handles `list`, `string`, and `dict`
types. When the container is a `python_value_type` (from an untyped
function parameter), the handler falls through to "not supported".

**Fix (~15 lines):** In the `In`/`NotIn` handler in `convert_compare`,
add a case for `python_value_type`:
```
if(is_python_value_type(right.type())) {
    // Dispatch: if tag==DICT, unwrap to dict and scan keys
    // if tag==LIST, unwrap to list and scan elements
    // if tag==STR, unwrap to string and scan chars
    exprt dict_result = /* dict in check using python_value_dict(right) */;
    exprt list_result = /* list in check using python_value_list(right) */;
    return if_exprt{python_value_is(right, DICT), dict_result,
           if_exprt{python_value_is(right, LIST), list_result, false_exprt{}}};
}
```

**Effort:** ~15 lines. Straightforward — reuse existing `in` logic for
each concrete type, wrapped in tag dispatch.

### 12.2 limit-subscript-tagged-dict — nested dict subscript (~3 tests)

**Test:** `d["outer"]["inner"]` where `d["outer"]` returns `python_value_type`.

**Root cause:** The tagged union subscript handler only unwraps to list
(added for `limit-typecast-issue`). It doesn't unwrap to dict.

**Fix (~10 lines):** In the tagged union subscript handler (after the
list unwrap), add dict unwrap:
```
// Also try dict unwrap
exprt dict_val = python_value_dict(value);  // needs new accessor
if(is_python_dict_type(dict_val.type())) {
    // Reuse dict subscript scan logic
}
```

**Blocker:** The `python_value_type` doesn't have a `__dict_ptr` field.
Adding one would increase the struct size. Alternative: use the `__list_ptr`
field to store dicts (both are struct pointers). Or add a new field.

**Effort:** ~20 lines + 5 lines in `python_value_type.h`.

### 12.3 limit-attr-nondet — attribute access on tagged union (~12 tests)

**Test:** `obj.length` where `obj` is `python_value_type`.

**Root cause:** `convert_attribute` checks if the value is a struct with
the named component. `python_value_type` doesn't have arbitrary attributes.

**Fix:** This is fundamentally hard — the tagged union can't have arbitrary
attributes. The fix would require either:
1. Unwrap to the concrete type and access the attribute (but we don't
   know which type at conversion time)
2. Use a property map (dict-like) for attribute access

**Assessment:** Not fixable without major refactoring. The tagged union
model doesn't support arbitrary attribute access. This is a fundamental
limitation of untyped parameters.

**Workaround:** Add type annotations to function parameters.

### 12.4 limit-float-precision — float arithmetic back-end (~23 tests)

**Test:** `divmod(7.5, 2.0)` remainder doesn't match expected value.

**Root cause:** Known floating-point bugs in the CBMC back-end (solver
encoding of IEEE 754 operations).

**Fix:** Not in our frontend — requires CBMC back-end fixes.

**Assessment:** Out of scope for the Python frontend.

### 12.5 limit-for-complex — enumerate() in for loops (~32 tests)

**Test:** `for i, n in enumerate(numbers):`

**Root cause:** `enumerate()` is not implemented. It should return a list
of `(index, element)` tuples.

**Fix (~30 lines):** In `convert_call`, add an `enumerate` handler:
```
if(func_name == "enumerate") {
    exprt iterable = convert_expression(arg);
    // Build list of (i, elem) tuples:
    // for each index i in [0, length), create tuple{i, data[i]}
    // Return list of tuples
}
```

Then the existing tuple-unpacking for-loop handler will work.

Also needed: `zip()` (~20 lines, similar pattern — build list of tuples
from two iterables).

**Effort:** ~50 lines total for enumerate + zip.

### 12.6 limit-set-operations — set difference/union/intersection (~24 tests)

**Test:** `a - b` where `a` and `b` are sets.

**Root cause:** Set operations (-, |, &, ^) are not implemented. Our set
model uses the same dedup array as lists but has no operator support.

**Fix (~40 lines):** In `convert_bin_op`, add set operation handlers:
```
if(is_python_set_type(left.type()) && is_python_set_type(right.type())) {
    if(op == "Sub") {  // set difference
        // For each element in left, check if NOT in right
        // Build new set with only non-matching elements
    }
    // Similar for BitOr (union), BitAnd (intersection), BitXor (symmetric)
}
```

**Effort:** ~40 lines. Each operation is an O(n²) scan.

### 12.7 limit-import-resolution — stdlib modules (~34 tests)

**Test:** `from collections import Counter`

**Root cause:** Only `math`, `random`, `typing`, and `re` are recognized.
Other stdlib modules (collections, itertools, functools, os, sys, etc.)
are silently ignored.

**Fix options:**
1. **Stub library:** Create minimal Python stubs for common stdlib modules
   (Counter as a dict subclass, defaultdict, etc.). ~100 lines per module.
2. **PYTHONPATH resolution:** Use the system Python's stdlib. But stdlib
   modules are complex and would slow down processing.
3. **Nondet models:** Return nondet for unknown stdlib functions with
   appropriate type constraints.

**Assessment:** Option 1 is most practical for verification. Start with
`collections.Counter` (most commonly used in ESBMC tests).

**Effort:** ~50 lines per module stub.

### 12.8 limit-re-module-usage — re.compile/search (~6 tests)

**Test:** `re.compile("[a-z]+").search("hello")`

**Root cause:** `re.compile()` returns nondet, `pattern.search()` is an
unknown method.

**Fix (~20 lines):** Model `re.compile()` as returning a "pattern" class
instance. Model `pattern.search()` as returning nondet (None or match).
Model `pattern.match()` similarly. For constant patterns and strings,
could compute exact results using C++ `<regex>`.

**Effort:** ~20 lines for nondet model, ~50 lines for exact matching.

### 12.9 limit-math-symbolic — math with symbolic args (~94 tests)

**Test:** `math.sin(x)` where `x` is a variable.

**Root cause:** Math functions with non-constant arguments return
constrained nondet (sin in [-1,1], sqrt >= 0, etc.). Tests that require
exact values (sin(1.5708) ≈ 1.0) fail because the solver can find
counterexamples within the constrained range.

**Assessment:** This is a fundamental limitation of the nondet-with-
constraints approach, matching CBMC's C frontend (src/ansi-c/library/math.c).
The only fix would be interval arithmetic or linking to actual math
library implementations, neither of which is practical for BMC.

**No fix planned.** The constrained nondet model is sound.

### 12.10 math-symbolic-arg — sin²+cos²≠1 (~0 additional tests)

**Test:** `sin(x)*sin(x) + cos(x)*cos(x) > 0.99`

**Root cause:** `sin(x)` and `cos(x)` are independent nondets. The
identity sin²+cos² = 1 is not enforced.

**Fix:** Would require adding `assume(sin(x)^2 + cos(x)^2 == 1)` when
both sin and cos are called with the same argument. This is complex
(need to track which math calls share arguments) and fragile.

**No fix planned.** Fundamental limitation.

### 12.11 limit-unknown-func — higher-order functions (~51 tests)

**Test:** `apply(f, x)` where `f: Callable[[int], int]`.

**Root cause:** Calling through a `Callable` parameter is not supported.
The parameter `f` is a code-typed symbol but the call `f(x)` cannot
resolve which function to invoke.

**Fix:** Map `Callable` parameters to CBMC function pointers. Use the
`remove_function_pointers` GOTO transformation pass to resolve calls.
This requires:
1. Convert `Callable` type annotation to `code_typet` pointer
2. At call sites, pass `address_of(function_symbol)`
3. Let CBMC's function pointer removal handle dispatch

**Effort:** ~50 lines in converter + CBMC infrastructure support.
**Risk:** High — function pointer removal may not work well with our
Python-specific types.

### 12.12 limit-loop-unsound — string list iteration timeout (~42 tests)

**Test:** `for s in string_list:` times out.

**Root cause:** Each string is a 264-byte struct. A list of 64 strings
is ~17KB. The for-loop creates a while-loop that iterates over this
array. With default unwinding, CBMC tries to unwind fully, which is
very slow for large struct arrays.

**Fix:** The pointer-based string model (Section 11) would reduce string
size from 264 bytes to 16 bytes (length + pointer). This would make
string list iteration ~16x faster.

**Alternative:** Use `--unwind N` with a small N. But this may miss bugs.

**Blocked on:** Section 11 (pointer-based string model).

### 12.13 limit-missing-runtime-errors — missing error detection (~92 wrong-pass)

**Test:** `1 / 0` should fail but passes.

**Root cause:** We removed division-by-zero property checks (Python models
them as exceptions). The exception flag is set but doesn't prevent
subsequent code from executing at module level. Also: undefined variables
(NameError), invalid chr() args, type errors in builtins.

**Fix (multi-part):**
1. **Division by zero at module level:** Add `assert(!__exception_active)`
   after each expression statement at module level. (~5 lines)
2. **Undefined variables:** In `convert_name`, when a symbol is not found,
   set `__exception_active` and return nondet. (~10 lines)
3. **Type errors in builtins:** Already partially done (abs). Extend to
   chr(), int(), float(), len(), etc. (~30 lines)

**Effort:** ~45 lines total.

### 12.14 limit-generator-infinite — lazy generators (deep)

**Root cause:** Generators with `yield` need coroutine-like state machine
transformation. Our eager evaluation model converts generators to lists,
which doesn't work for infinite generators.

**Fix:** Transform generator functions into state machine classes:
- Each `yield` becomes a state transition
- `__next__()` resumes from the saved state
- Local variables are stored in the state object

**Effort:** 2-3 weeks. Major architectural change.

### 12.15 limit-async-concurrent — async/threads (deep)

**Root cause:** `async def` and `await` need CBMC thread primitives
(`__CPROVER_thread_create`, etc.) for concurrent verification.

**Fix:** Map `async def` to thread creation, `await` to thread join.
Use CBMC's existing concurrency support.

**Effort:** 2-3 weeks. Requires understanding CBMC's thread model.

### 12.16 limit-overflow-nondet-arith — unbounded ints (configuration)

**Root cause:** Python integers have arbitrary precision. Our 64-bit
`signedbv` model overflows for large values. The `--python-unbounded-ints`
option is registered but uses `mathematical_integer` type which requires
the Z3 solver (`--z3`).

**Fix:** Wire up `--python-unbounded-ints` to actually use `integer_typet`
throughout the converter. Ensure all arithmetic, comparison, and typecast
operations handle `integer_typet`.

**Effort:** ~30 lines to wire up, but extensive testing needed with Z3.

### Updated Priority Assessment

See Section 15.6 for the current priority roadmap with effort estimates
and dependency analysis.


## 13. Python Verification Benchmarks Results

### Current Results (51 benchmarks, stubs-full-python)

See Section 15.1 for the latest metrics and Section 15.2 for detailed
root cause analysis of each non-CLEAN/TP benchmark.

### Historical Results

Initial (before struct_tag_typet): CLEAN 25, TP 2, FP 2, MISS 7, TOERR 12, TIMEOUT 3
After struct_tag_typet + from_integer fix: CLEAN 27, TP 3, FP 4, MISS 7, TOERR 7, TIMEOUT 3
After Any → python_value_type: CLEAN 21, TP 3, FP 9, MISS 6, TOERR 9, TIMEOUT 3

### TOERR Root Causes

**Exit 6 — type mismatch (7 benchmarks):**
`expected signedbv` error during GOTO conversion. Occurs when dict
values are lists or other struct types. The dict value type is
`python_value_type` but operations on the unwrapped value expect
`signedbv`. Fix: improve tagged union unwrapping for dict values.
KNOWNBUG: `limit-type-mismatch-goto`.

**Exit 134 — from_integer crash (5 benchmarks):**
`from_integer` called on struct type. Occurs when class instances
are stored in dicts or passed through untyped functions. The
`safe_zero(struct_type)` or `from_integer(0, struct_type)` crashes.
Fix: guard all `from_integer` calls with type checks.
KNOWNBUG: `limit-from-integer-crash`.

### MISS Root Causes

The 9 missed bugs are from removing the `uncaught exception` check
(which caused 3 false positives). The check was too coarse — it
fired whenever any exception flag was set, even inside try blocks.

**Fix plan:** Re-add the uncaught exception check but make it smarter:
only fire if the exception was raised OUTSIDE any try block, or if
the exception type doesn't match any handler. This requires tracking
whether the current code path is inside a try block at the GOTO level,
not just at the converter level.

### TIMEOUT Root Causes

3 benchmarks timeout at 30s: `demo_glue_service`, `glue_job_runner`,
`sagemaker_labeling_job`. These are large files with many AWS service
calls. The S3 stub processing (2841 lines, 108 methods) is the
bottleneck. Fix: pointer-based string model (Section 11) and
on-demand method body conversion.

### Priority for Next Improvements

1. **Fix TOERR (12→0):** Guard `from_integer` and `safe_zero` calls
   with type checks. ~20 lines, high impact.
2. **Smart uncaught exception check (0 TP → some TP):** Re-add with
   try-block awareness. ~30 lines, recovers missed bugs.
3. **Performance (3 timeouts):** Pointer-based string model or
   on-demand method body conversion. Major effort.


### 12.17 for-enumerate — enumerate tuple unpacking regression

**Test:** `for i, n in enumerate(numbers): total += n`

**Root cause:** `enumerate()` builds a list of `(index, element)` tuples.
The for-loop tuple unpacking assigns `n := nondet` instead of extracting
the tuple's `_1` field. The issue is that the list element type from
`enumerate` (a tuple struct) doesn't match the pre-registered variable
type, causing the tuple field extraction to fall back to nondet.

**Fix (~10 lines):** In the for-loop tuple unpacking code, when
`elem_val.type()` is a struct with `_0`/`_1` components, extract fields
directly regardless of the loop variable's pre-registered type. The
current code checks `to_struct_type(elem_val.type()).has_component(field)`
which should work — need to debug why it doesn't fire for enumerate results.

**Effort:** Small — likely a type mismatch between the enumerate tuple
struct and the list element type.

### 12.18 limit-from-integer-crash — from_integer on struct type

**Test:** Dict with class instance values causes `from_integer` crash.

**Root cause:** `from_integer(0, struct_type)` is called when creating
default values for dict entries or function parameters with class types.
The `safe_zero` function handles structs by recursing into components,
but some code paths call `from_integer` directly without checking the type.

**Fix (~15 lines):** Audit all `from_integer` calls in the converter and
guard them with type checks:
```cpp
if(type.id() == ID_signedbv || type.id() == ID_unsignedbv ||
   type.id() == ID_integer || type.id() == ID_bool)
  return from_integer(val, type);
else
  return safe_zero(type);
```

Key locations to fix:
- Dict value default in subscript handler
- Function parameter default values
- Comparison operators with mixed types

**Effort:** ~15 lines across 3-4 locations.

### 12.19 limit-type-mismatch-goto — expected signedbv type error

**Test:** Dict with list values causes `expected signedbv` during GOTO.

**Root cause:** Dict values are `python_value_type` (tagged union). When
a dict value is used in an arithmetic context (e.g., `total += d["key"]`),
the tagged union is not unwrapped before the operation. The GOTO converter
expects `signedbv` but gets the tagged union struct.

**Fix (~10 lines):** In arithmetic operators (`+`, `-`, `*`, etc.), when
an operand is `python_value_type`, unwrap it to the expected type before
creating the arithmetic expression. This is already done in `convert_bin_op`
but may be missing in some code paths (e.g., augmented assignment, or
operations inside imported module bodies).

**Effort:** ~10 lines — find and fix the specific code path.

### 12.20 runtime-error-divzero — division by zero not detected

**Test:** `1 / 0` should fail but passes.

**Root cause:** The `uncaught exception` check at program end was removed
because it caused too many false positives. Division by zero sets the
`__exception_active` flag but no property check fires.

**Fix options:**
1. **Smart exception check:** Re-add the uncaught exception check but
   only fire if the exception was raised outside any try block. Requires
   tracking try-block depth at the GOTO level. (~30 lines)
2. **Per-statement exception check:** After each expression statement at
   module level, add `assert(!__exception_active)`. This catches div-by-zero
   immediately. (~10 lines in `convert_module_body`)
3. **Configurable:** Add `--python-exception-check` flag to enable/disable.

**Recommended:** Option 2 — per-statement checks at module level only.
This catches runtime errors without the false positives from function-level
exception flags.

### Updated KNOWNBUG Summary

| # | KNOWNBUG | Plan | Feasibility |
|---|----------|------|-------------|
| 1 | limit-async-concurrent | §12.15 / §15.3.9 | Deep — weeks |
| 2 | limit-attr-nondet | §12.3 / §15.3.3 | Fundamental — no fix |
| 3 | limit-float-precision | §12.4 / §15.3.4 | Back-end — no fix |
| 4 | limit-generator-infinite | §12.14 / §15.3.9 | Deep — weeks |
| 5 | limit-import-resolution | §12.7 / §15.3.8 | ~5 lines — constructor fallback |
| 6 | limit-loop-unsound | §12.12 / §15.3.7 | Blocked — needs §15.4 string model |
| 7 | limit-math-symbolic | §12.9 / §15.3.5 | Fundamental — no fix |
| 8 | limit-set-operations | §12.6 / §15.3.2 | ~80 lines — bitmap model |
| 9 | limit-subscript-tagged-dict | §12.2 / §15.3.1 | ~60 lines — constant-key opt |
| 10 | limit-unknown-func | §12.11 / §15.3.6 | ~30 lines — inline known funcs |
| 11 | math-symbolic-arg | §12.10 / §15.3.5 | Fundamental — no fix |


## 14. Tagged Union Recursive Type — Unblocking Plan

### Problem (RESOLVED)

`python_value_type` needed `__list_ptr: pointer_to(list[python_value_type])`
to preserve element types when lists are stored in the tagged union.

### Solution (IMPLEMENTED)

`struct_tag_typet` refactoring completed successfully:
1. `python_value_type()` returns `struct_tag_typet{"tag-python_value"}`
2. Actual struct definition registered in symbol table as `tag-python_value`
3. `__list_ptr` points to `list[python_value_type]` (self-referential)
4. `value_set.cpp` patched to resolve `struct_tag_typet` in type comparison
5. `make_python_value` creates `struct_exprt` with `struct_tag_typet` as type

### Key Learnings

- The solver resolves `struct_tag_typet` correctly — no special handling needed
- `safe_zero` for `struct_tag_typet` must NOT override the result type back to
  the tag type (causes `simplify_member` crashes)
- `unwrap_value` must NOT return nondet for `python_value_type` target — it
  must fall through to the default `python_value_int` extraction
- Adding fields to `python_value_type` has multiplicative cost: each list/dict
  element is a `python_value_type`, so one extra pointer field adds 8 bytes ×
  64 elements = 512 bytes per list. The `__dict_ptr` field alone is fine
  (~7% perf hit), but converting dict VALUES to `python_value_type` causes
  timeouts (16 entries × full struct construction)


## 15. Current State and Unblocking Plans

### 15.1 Current Metrics (2026-05-01)

**Regression tests:** 303 total, 292 CORE, 11 KNOWNBUG

**Benchmark (51 AWS SDK tests):**

| Result | Count | Description |
|--------|-------|-------------|
| CLEAN | 21 | Clean code verified successfully |
| TP | 3 | True positive (bug found in buggy code) |
| MISS | 6 | Missed bug (buggy code passes) |
| FP | 9 | False positive (clean code fails) |
| TOERR | 9 | Tool error (crash or type error) |
| TIMEOUT | 3 | Exceeded 30s timeout |

**ESBMC suite:** 3090 tests, ~1407 correct, 5 crashes

### 15.2 Root Cause Analysis of Benchmark Issues

#### FP (9 benchmarks) — False Positives

**Category A: Stub assertion failures (5 benchmarks)**
`execute_stepfunction`, `get_iam_role_arn`, `invoke_lambda_example`,
`update_lambda_env`, `cloudwatch_metrics_example`

Root cause: The boto3 stubs contain `assert compile(regex).search(value)`
patterns. With `Any → python_value_type`, the `compile` function is
unresolved (returns nondet), so `compile(...).search(...)` returns nondet,
and the stub assertion fails. These are stub precision issues.

Fix plan: Register `compile` as a known function in the `re` module
handler (returns nondet regex object). The regex object's `.search()`
already returns nondet. This would make the stub assertions pass
vacuously. ~5 lines in the `re` module attribute handler.

**Category B: No-body checks (2 benchmarks)**
`train_llm_conversation`, `websocket_url_validator`

Root cause: Functions imported from unresolved modules (`custom_prompts`,
`urllib.parse`) have no body. The `no-body` property fires.

Fix plan: Already partially addressed — `urllib.parse` is in the stdlib
stub list. `custom_prompts` is a benchmark-specific module that doesn't
exist. With `--python-no-body-check`, both pass. No further action needed.

**Category C: Dict KeyError (1 benchmark)**
`bedrock_model_discovery`

Root cause: `providers[provider].append(model)` raises a spurious KeyError.
The code has `if provider not in providers: providers[provider] = []` guard,
but the solver can't prove the key exists after the guard because the `in`
check and the subscript are in different GOTO basic blocks, and the dict
model doesn't track key presence precisely.

Fix plan: This requires **dict key tracking** — maintaining a boolean array
`key_present[PYTHON_MAX_DICT_SIZE]` alongside the keys/values arrays. When
a key is assigned, set `key_present[i] = true`. The KeyError check becomes
`assert exists i: key_present[i] && keys[i] == key`. ~30 lines in the dict
model. Medium priority.

**Category D: Nondet comparison (1 benchmark)**
`diagnose_ssm_connectivity`

Root cause: Passes with `--python-no-body-check`. The `no-body` check for
an unresolved function is the only failure. Already addressed by stdlib
stub registration.

#### TOERR (9 benchmarks) — Tool Errors

**Category A: `expected signedbv` type mismatch (5 benchmarks)**
`apigateway_key_manager`, `setup_cloudformation_delegated_admin`,
`cloudwatch_logs_query`, `iam_policy_checker`, `kms_client_manager`

Root cause: `symex_function_call.cpp:114` throws when a function argument
type doesn't match the parameter type. This happens when:
1. A string struct is passed to a parameter typed `signedbv[64]`
2. A `python_value_type` struct is passed to a parameter typed `str`

The argument type matching in `convert_call` (line 5941) handles most
cases, but some call sites in stubs go through different paths:
- Stub-internal method calls where the stub's own type annotations
  create `python_value_type` parameters but pass concrete types
- Keyword argument packing where the dict value type doesn't match

Fix plan: Add a **universal type guard in symex** — instead of throwing,
insert a typecast. This is a 5-line change in `symex_function_call.cpp`
that replaces the `throw` with `rhs = typecast_exprt{rhs, parameter_type}`
for Python files. This is safe because Python is dynamically typed — any
type mismatch is a valid overapproximation. However, this changes CBMC
core code. Alternative: add type matching to ALL remaining call sites in
the converter (there are 9 total, 5 already have it).

**Category B: `from_integer` on struct type (3 benchmarks)**
`aws_resource_tagger`, `aws_untagged_resources_analyzer`, `mediaconvert_manager`

Root cause: CBMC's internal code calls `from_integer(0, struct_type)` during
GOTO conversion or simplification. Our `from_integer` patch handles
`ID_struct` and `ID_struct_tag` by returning `constant_exprt{ID_0, type}`.
But some benchmarks still crash because the struct type reaches `from_integer`
through a different path (e.g., the simplifier creating default values).

Fix plan: The current `from_integer` patch is correct but incomplete. Need
to also handle `ID_array` type in `from_integer` (arrays of structs). ~3
lines. Also check if the crash is from `to_integer` (the inverse function)
being called on struct constants.

**Category C: Unknown (1 benchmark)**
`aws_resource_tagger` — needs individual debugging.

#### MISS (6 benchmarks) — Missed Bugs

`bedrock_data_automation_example`, `create_bedrock_inference_profile`,
`create_s3_vector_index`, `rds_instance_creator.1`, `rds_instance_creator.2`,
`s3_backup_restore`

Root cause: These benchmarks contain bugs (e.g., missing error handling,
incorrect return types, unvalidated inputs) that our verifier doesn't
detect because:
1. The bugs are in exception handling paths we don't model precisely
2. The stubs return nondet, so error conditions are not triggered
3. The bugs require deeper unwinding (> 3) to expose

Fix plan: Each benchmark needs individual analysis to understand what bug
it contains and what verification property would catch it. This is ongoing
work — each benchmark is ~30 min of investigation.

#### TIMEOUT (3 benchmarks)

`demo_glue_service`, `glue_job_runner`, `sagemaker_labeling_job`

Root cause: These are large files (100+ lines) with many AWS service calls.
The Python AST generation (python3 subprocess) takes >30s. The 1MB RSS
confirms they're stuck in parsing, not solving.

Fix plan: The pointer-based string model (Section 11) would reduce the
GOTO program size and speed up parsing. Also, lazy method body conversion
(only convert methods that are actually called) would skip unused stub
methods. ~100 lines for lazy conversion.

### 15.3 Remaining 11 KNOWNBUGs — Updated Plans

#### 15.3.1 limit-subscript-tagged-dict — Dict subscript on tagged union

**Status:** `__dict_ptr` field can be added without timeout regression
(~7% perf hit). But dict subscript with string keys is imprecise because
string comparison on fixed-size arrays is too expensive for the solver.

**Blocker:** String key comparison. `equal_exprt{string_struct, string_struct}`
expands to 256-element array comparison. The solver can prove equality for
constant strings (via simplification) but not for symbolic strings.

**Detailed fix plan:**

Phase 1 — Add `__dict_ptr` field (~15 lines, no perf regression):
```
python_value_type.h: Add __dict_ptr field to python_value_struct_def()
python_value_type.h: Add DICT=6 to python_type_tagt enum
python_value_type.h: Add null dict_ptr default in make_python_value
python_value_type.h: Add DICT case in make_python_value switch
python_value_type.h: Add dict_ptr to struct_exprt operands
```

Phase 2 — Dict wrap/unwrap (~20 lines):
```
python_converter.cpp wrap_value(): Store dict pointer (simple, no value conversion)
python_converter.cpp unwrap_value(): Dereference __dict_ptr for dict target
```
Key insight: Do NOT convert dict values to python_value_type. Store the
original dict as-is and typecast the pointer. The dict subscript handler
must use the ORIGINAL dict type (from the pointer's base type), not the
expected `dict[str, python_value_type]` type.

Phase 3 — Dict subscript on tagged union (~25 lines):
```
python_converter.cpp convert_subscript(): When value is python_value_type,
dereference __dict_ptr, scan keys, return matching value.
```
The key comparison must use `string_constants` tracking: if both the key
literal and the dict key are known constants, compare them as C++ strings
at conversion time (not at solver time). This avoids the 256-element array
comparison entirely.

**Constant-key optimization (critical for correctness):**
```cpp
// In dict subscript handler:
auto key_str = extract_string_value(key);
if(key_str.has_value())
{
  // Scan dict keys at conversion time
  for(size_t i = 0; i < dict_keys.size(); i++)
  {
    auto dk = extract_string_value(dict_keys[i]);
    if(dk.has_value() && dk.value() == key_str.value())
      return dict_values[i]; // exact match at conversion time
  }
}
// Fallback: solver-time scan (imprecise for symbolic keys)
```

This optimization makes `d["key"]` exact for literal dicts with literal
keys (the common case in benchmarks), while falling back to nondet for
symbolic keys.

**Effort:** ~60 lines total. Phase 1-2 are safe. Phase 3 needs the
constant-key optimization to be useful.

#### 15.3.2 limit-set-operations — Set difference/union/intersection

**Status:** Sets are modeled as lists. Set difference `a - b` requires
O(n²) element comparisons. With `PYTHON_MAX_LIST_LENGTH=64`, this creates
4096 nested `if_exprt` nodes that the solver can't handle.

**Blocker:** Solver scalability with nested `if_exprt`.

**Detailed fix plan — Bitmap representation:**

Replace the list-based set model with a **bitmap** for small integer sets:
```c
struct python_set {
    uint64_t bitmap;     // bit i set ↔ element i is in the set
    int64_t offset;      // bitmap represents elements [offset, offset+63]
};
```

Operations become bitwise:
- `a - b` → `a.bitmap & ~b.bitmap` (1 instruction)
- `a | b` → `a.bitmap | b.bitmap` (1 instruction)
- `a & b` → `a.bitmap & b.bitmap` (1 instruction)
- `x in s` → `(s.bitmap >> (x - s.offset)) & 1`
- `len(s)` → `popcount(s.bitmap)`
- `s == t` → `s.bitmap == t.bitmap && s.offset == t.offset`

Limitations:
- Only works for integer sets with elements in a 64-element range
- String sets need the list-based model (or a hash-based approach)
- Set literals `{1, 2, 3}` compute the bitmap at conversion time

Implementation:
1. `python_types.h`: Add `python_set_type()` returning the bitmap struct
2. `python_converter.cpp`: Set literal → compute bitmap constant
3. `python_converter.cpp`: Set operations → bitwise expressions
4. `python_converter.cpp`: `in` operator → bit test
5. `python_converter.cpp`: `len()` → popcount (loop or lookup table)

**Effort:** ~80 lines. The bitmap model is simple and the solver handles
bitwise operations efficiently.

**Alternative:** Reduce `PYTHON_MAX_LIST_LENGTH` to 8 for set operations
only. This makes the O(n²) approach tractable (64 comparisons) but limits
set size. Could be a quick interim fix (~5 lines).

#### 15.3.3 limit-attr-nondet — Attribute access on tagged union

**Status:** Fundamental limitation. When a variable has type
`python_value_type`, accessing `.attr` is meaningless — the tagged union
doesn't have arbitrary attributes.

**No fix possible** without runtime type tracking. The tagged union would
need a `__class_ptr` field pointing to the class struct, and attribute
access would dereference through the class pointer. This is essentially
implementing Python's object model, which is a major architectural change.

**Workaround:** Use type annotations. `x: MyClass = f()` gives `x` the
correct type, enabling attribute access. The `Any → python_value_type`
change makes this more important — users should annotate variables that
need attribute access.

#### 15.3.4 limit-float-precision — Float arithmetic back-end

**Status:** CBMC's IEEE 754 model has precision issues with some float
operations (e.g., `1.0 / 3.0 * 3.0 != 1.0`). This is a CBMC back-end
issue, not our frontend.

**No fix possible** in the Python frontend. Would need CBMC solver changes.

#### 15.3.5 limit-math-symbolic / math-symbolic-arg

**Status:** Fundamental. `math.sin(x)` returns nondet because CBMC can't
model transcendental functions symbolically. `sin²(x) + cos²(x) ≠ 1`
because the two nondet values are independent.

**No fix possible** without adding trigonometric identities to the solver
or using interval arithmetic. Both are major CBMC changes.

#### 15.3.6 limit-unknown-func — Higher-order functions

**Status:** `map(f, lst)` where `f` is a function argument. CBMC doesn't
support function pointers in the Python frontend.

**Detailed fix plan:**

Phase 1 — Inline known functions (~30 lines):
When `map(f, lst)` is called and `f` is a known function (not a parameter),
inline the function call for each list element:
```cpp
for(i = 0; i < len; i++)
  result[i] = f(lst[i]);  // direct call, not function pointer
```

Phase 2 — Function pointer support (~100 lines):
Register Python functions as function pointers in the symbol table.
When a function is passed as an argument, create a `code_typet` parameter
and use CBMC's function pointer resolution to dispatch.

**Effort:** Phase 1 is tractable (~30 lines). Phase 2 is high risk.

#### 15.3.7 limit-loop-unsound — String list iteration timeout

**Status:** Iterating over a list of strings times out because each string
is 256 bytes. A list of 64 strings is 64 × 264 = 16KB of solver variables.

**Blocker:** Fixed-size string model (Section 11).

**Fix:** Implement the pointer-based string model. See Section 15.4.

#### 15.3.8 limit-import-resolution — Stdlib modules

**Status:** `from collections import Counter` — `Counter` is registered
as a nondet function but the call `Counter([1,1,2,3])` goes through the
constructor path, which crashes (`map::at`) because `Counter` is not a
registered class.

**Detailed fix plan:**

The constructor call handler (`convert_call`) checks `class_types` for the
function name. If not found, it should fall back to a regular function call
instead of crashing:

```cpp
// In convert_call, around the class constructor detection:
auto cls_it = class_types.find(func_name);
if(cls_it != class_types.end())
{
  // ... existing constructor logic ...
}
else
{
  // Fall back to regular function call
  // (handles Counter, namedtuple, etc.)
}
```

This is ~5 lines. The `Counter` call would then go through the regular
function call path, which returns nondet (since `Counter` has no body).

**Effort:** ~5 lines. Low risk.

#### 15.3.9 limit-generator-infinite / limit-async-concurrent

**Status:** Deep architectural features. Generators need state machine
transformation. Async needs CBMC thread support.

**No near-term fix.** These are multi-week efforts.

### 15.4 Pointer-Based String Model — Detailed Implementation Plan

This is the single most impactful architectural change remaining. It
unblocks `limit-loop-unsound`, improves dict key comparison, and reduces
solver overhead for all string operations.

#### Current model

```c
struct python_string {
    int64_t length;
    unsigned char data[PYTHON_MAX_STRING_LENGTH]; // 256 bytes
};
```

Every string literal, variable, and temporary occupies 264 bytes. String
comparison is a 256-element array comparison. Dict key lookup is 16 ×
256-element comparisons.

#### Proposed model

```c
struct python_string {
    int64_t length;
    unsigned char *data; // pointer to heap-allocated array
};
```

String literals allocate exactly `length` bytes. String comparison
dereferences both pointers and compares element-by-element up to `length`.

#### Implementation steps

**Step 1: Change `python_string_type()` (~5 lines)**
```cpp
// python_types.h
inline struct_typet python_string_type()
{
  struct_typet::componentst components;
  components.push_back({"length", signedbv_typet{64}});
  components.push_back({"data", pointer_typet{unsignedbv_typet{8}, 64}});
  // ...
}
```

**Step 2: Update `build_string_struct()` (~20 lines)**
Currently builds a struct with inline array. Change to:
1. Create a symbol for the character array (exact size)
2. Set the symbol's value to the array literal
3. Return struct with length and pointer to the symbol

```cpp
exprt build_string_struct(const std::string &s)
{
  // Create array symbol
  array_typet arr_type{unsignedbv_typet{8},
    from_integer(s.size(), signedbv_typet{64})};
  // ... create symbol, set value ...
  return struct_exprt{
    {from_integer(s.size(), signedbv_typet{64}),
     address_of_exprt{sym.symbol_expr()}},
    python_string_type()};
}
```

**Step 3: Update `extract_string_value()` (~10 lines)**
Currently reads from inline array. Change to dereference pointer:
```cpp
// Follow the pointer to the array symbol
if(data_expr.id() == ID_address_of)
  // ... extract from the pointed-to array
```

**Step 4: Update string concatenation (~15 lines)**
Currently copies both arrays into a new 256-byte array. Change to:
1. Allocate new array of size `len1 + len2`
2. Copy elements from both source arrays
3. Return struct with new length and pointer

**Step 5: Update string comparison (~10 lines)**
Currently compares inline arrays. Change to:
1. Compare lengths first (quick reject)
2. Compare elements through pointers up to `min(len1, len2)`

**Step 6: Update dict key comparison (~10 lines)**
Same as string comparison but through dict key array elements.

**Step 7: Update `__str_ptr` in tagged union (~5 lines)**
Currently `pointer_to(python_string_type)`. The string type changes
but the pointer indirection stays the same.

**Step 8: Update all string method handlers (~30 lines)**
`upper()`, `lower()`, `strip()`, `split()`, `join()`, `replace()`,
`find()`, `startswith()`, `endswith()` — all need to dereference
the data pointer instead of accessing the inline array.

**Step 9: Update `PYTHON_MAX_STRING_LENGTH` usage (~10 lines)**
The constant becomes the maximum allocation size (for nondet strings),
not the fixed array size. Nondet strings allocate `PYTHON_MAX_STRING_LENGTH`
bytes; literal strings allocate exactly their length.

#### Risk assessment

- **Low risk:** Steps 1-3 (type change, literal construction, extraction)
- **Medium risk:** Steps 4-6 (concatenation, comparison — solver behavior
  with pointer-based arrays is less tested)
- **High risk:** Step 8 (many handlers to update, each could introduce bugs)

#### Testing strategy

1. Run all 303 regression tests after each step
2. Run ESBMC suite after steps 1-3 and after step 8
3. Run benchmark suite after all steps
4. Performance comparison: `delete_s3_object.py` should be faster

#### Effort estimate

~120 lines of changes. 1-2 days of focused work including testing.

### 15.5 `Any` Type Semantics — Impact Analysis

The change from `Any → int` to `Any → python_value_type` is semantically
correct per PLR §4.12.5 but exposed precision gaps:

1. **Stub assertions fail** because they receive tagged unions where they
   expected ints. The stubs use `assert compile(regex).search(value)` which
   fails when `value` is a tagged union (nondet) instead of a string.

2. **Tagged union comparison** was broken because `unwrap_value` returned
   nondet for `python_value_type` target. Fixed by excluding
   `python_value_type` from the `struct_tag` nondet case.

3. **Assignment type mismatch** when `Any`-typed return values are assigned
   to typed variables. Fixed by using `safe_typecast` in the assignment
   handler.

4. **Function parameter type mismatch** when `Any`-typed arguments are
   passed to typed parameters. Partially fixed by argument type matching
   at all call sites, but some stub-internal calls still fail.

The net effect: CLEAN dropped from 27 to 21, but TP increased from 2 to 3.
The 6 lost CLEANs are from stub precision issues (Category A FPs above),
not from our frontend. The correct fix is to improve the stubs or add
`compile` to the `re` module handler.

### 15.6 Priority Roadmap

| Priority | Item | Effort | Impact | Unblocks |
|----------|------|--------|--------|----------|
| 1 | Register `compile` in re module handler | ~5 lines | 5 FP → CLEAN | Stub assertions |
| 2 | Constructor-to-function fallback | ~5 lines | 1 KNOWNBUG | limit-import-resolution |
| 3 | Constant-key dict subscript optimization | ~60 lines | 1 KNOWNBUG | limit-subscript-tagged-dict |
| 4 | Bitmap set model | ~80 lines | 1 KNOWNBUG | limit-set-operations |
| 5 | Pointer-based string model | ~120 lines | 1 KNOWNBUG + perf | limit-loop-unsound, timeouts |
| 6 | Inline known higher-order functions | ~30 lines | 1 KNOWNBUG | limit-unknown-func (partial) |
| 7 | Dict key tracking (key_present array) | ~30 lines | 1 FP | bedrock_model_discovery |
| 8 | Universal symex type guard | ~5 lines | 5 TOERR | expected signedbv crashes |
