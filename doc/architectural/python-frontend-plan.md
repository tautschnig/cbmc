# Python Front-End for CBMC — Design and Implementation Plan

**User guide:** [doc/python-verification-guide.md](../python-verification-guide.md)

## Status

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Skeleton & infrastructure | **Complete** |
| 2 | Scalar expressions | **Complete** |
| 3 | Control flow | **Complete** |
| 4 | Type inference | **Complete** |
| 5 | Strings | **Complete** (concat tracks length, not content) |
| 6 | Collections | **Complete** (list, tuple, dict) |
| 7 | Classes and objects | **Complete** (pointer-based model) |
| 8 | Exception handling | **Complete** (raise, try/except, uncaught detection) |
| 9 | Advanced features | **Partial** (Tiers 1-3 of tagged unions done) |

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

### KNOWNBUG inventory (1 test)

Items 1-6 from the original plan have been implemented. Only the
architecture-level change remains.

---

#### `type-change` — Variable changes type during execution

**Tiered approach:**

**Tier 1 (DONE):** Fresh variable renaming for straight-line type changes.
When `x = 5; x = "hello"` is encountered, the second assignment creates
`python::x__v1` with type `str`. Subsequent references to `x` resolve to
the latest version. No overhead for the solver.

**Tier 2:** Merge-point ambiguity. When `if/else` branches assign
different types to the same variable, the merge point has an ambiguous
type. Options: (a) emit an error, (b) use a tagged union at the merge.
Remaining KNOWNBUG: `type-change-conditional`.

**Tier 3:** Tagged unions for genuinely unknown types. Needed for:
- `--function` with unannotated parameters (Case A)
- Functions with no return annotation (Case B)
- Heterogeneous lists (Case C)

Remaining KNOWNBUG: `type-untyped-function`, `type-unknown-return`,
`type-heterogeneous-list`.

**Implementation steps:**

Phase 9 is a fundamental architecture change. The implementation plan:

**Step 1: Define the universal Python value type**
```
struct python_value_t {
  int type_tag;  // 0=none, 1=int, 2=float, 3=bool, 4=str, 5=list, ...
  union {
    int64_t int_val;       // or integer_typet for unbounded
    double float_val;
    bool bool_val;
    python_str_t str_val;
    python_list_t list_val;
    // ... one field per supported type
  };
};
```

**Step 2: Update convert_type_annotation**
- When no annotation is present, use `python_value_t` instead of defaulting
  to int
- When annotation is present, still use the specific type (optimization)

**Step 3: Update all expression converters**
- Every operation must dispatch on the type tag
- `x + y` becomes:
  ```
  if(x.tag == INT && y.tag == INT) result = {INT, x.int_val + y.int_val}
  else if(x.tag == FLOAT || y.tag == FLOAT) result = {FLOAT, ...}
  else if(x.tag == STR && y.tag == STR) result = {STR, concat(...)}
  else assert(false, "TypeError")
  ```
- This multiplies the formula size significantly

**Step 4: Update convert_assign**
- Assignment to a `python_value_t` variable sets the tag and the
  appropriate union field
- No typecast needed — the variable can hold any type

**Step 5: Update convert_name**
- Reading a `python_value_t` variable extracts the value based on context
- Or returns the full tagged union for further dispatch

**Step 6: Optimization — type narrowing**
- After `isinstance(x, int)` or `if type(x) == int`, narrow the type
  to avoid the dispatch overhead
- Use CBMC's assume mechanism: `assume(x.tag == INT)`

**Affected files:** All converter files, `python_types.h`, potentially
`expr2python.cpp`

**Estimated effort:** 2-3 weeks

**Dependencies:** None (but benefits from all other features being stable)

**Risk:** Formula explosion. Every operation on a tagged union generates
a multi-way branch. For programs that use type annotations (the common
case for verification), this overhead is unnecessary. The mitigation is
to only use tagged unions for variables without annotations, and use
specific types for annotated variables (the current behavior).

---

### Summary

Items 1-6 have been implemented. Only item 7 remains:

| # | Test | Status |
|---|------|--------|
| 1 | `list-subscript-assign` | **DONE** |
| 2 | `generator-expression` | **DONE** (literal iterables) |
| 3 | `set-literal` | **DONE** |
| 4 | `slice-expression` | **DONE** |
| 5 | `del-statement` | **DONE** |
| 6 | `import-value` | **DONE** (math module models) |
| 7 | `type-change` | Tier 1 DONE; Tiers 2-3 need tagged unions |

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
