# Python Front-End for CBMC — Design and Implementation Plan

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
| 8 | Exception handling | **Partial** (raise-as-failure, try Stage A) |
| 9 | Advanced features | Not started |

### Cross-cutting features

| Feature | Status |
|---------|--------|
| `--function` mode | **Complete** |
| `expr2python` traces | **Complete** |
| Division-by-zero checks | **Complete** |
| Index-out-of-bounds checks | **Complete** |
| `raise` as verification failure | **Complete** |
| ESBMC failure pattern tests | **Complete** (7 tests from evaluation) |
| Parameterized type annotations | **Complete** |
| Global variable access | **Complete** |
| Dict literals | **Complete** |
| `with` statement | **Complete** (simplified, no __enter__/__exit__) |
| Built-in functions | **Complete** (int, float, bool, abs, min, max, print, len) |
| Bitwise operators | **Complete** |
| `try`/`except` Stage A | **Complete** |

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

### 6.1 Parameterized type annotations (DONE)
### 6.2 Global variables accessed from functions (DONE)
### 6.3 Dict literals and access (DONE)
### 6.4 `with` statement (DONE)

### 6.5 `try`/`except` (DONE — Stage A)

Stage A implemented: execute try body, skip except handlers.

### 6.6 Remaining gaps (from ESBMC benchmark validation)

Validation against 2,089 ESBMC non-fail tests: 1,242 pass (59%).
Main gaps by impact:

- **Generator expressions** (`all(x > 0 for x in l)`) — 13+ tests.
  Would need to desugar to loops.
- **List comprehensions** (`[x*2 for x in lst]`) — 13+ tests.
  Desugar to loop + append.
- **Lambda** (`lambda x: x+1`) — 10+ tests.
  Model as anonymous function symbols.
- **`isinstance()`** — 14+ tests.
  Needs type tag on class instances.
- **Import/ImportFrom affecting control flow** — 77+ tests.
  Currently silently ignored; some tests depend on imported values.
- **Augmented assign on subscripts** (`lst[0] += 1`) — several tests.
- **String ordering** (`"A" < "B"`) — several tests.
- **Slice expressions** (`lst[1:3]`) — 8+ tests.

### Previous items (DONE)

- Pointer-based class instances ✓
- KNOWNBUG backlog (tuple unpack, string concat, list append) ✓
- `--function` mode with nondet harness ✓
- Readable counterexample traces ✓
- Division-by-zero and bounds checks ✓
- `raise` as verification failure ✓

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
