# Verifying Python Programs with CBMC

CBMC includes a Python front-end that can verify Python 3 programs using
bounded model checking. This guide explains how to use it.

For frontend internals (passes, type system, generator semantics, etc.)
see [python-frontend-architecture.md](python-frontend-architecture.md).
For the open-work backlog see
[python-frontend-plan.md](python-frontend-plan.md).

## Quick Start

```bash
# Verify a Python file (top-level code is the entry point)
cbmc program.py

# Verify a specific function with nondet inputs
cbmc program.py --function my_function

# Use correct Python integer semantics (requires Z3)
cbmc program.py --python-unbounded-ints --z3
```

## What CBMC Checks

CBMC automatically checks for:

- **User assertions**: `assert condition` statements
- **Division by zero**: `a // b` and `a % b` where `b` could be zero
- **Index out of bounds**: `lst[i]` where `i` could be outside `[0, len)`
- **Integer overflow**: `a + b`, `a * b` where the result could overflow
  int64 (when not using `--python-unbounded-ints`)
- **Uncaught exceptions**: `raise` statements that propagate without
  being caught by `try`/`except`
- **Missing function bodies**: calls to undefined functions

## Recommended Verification Flag Suite

For real-world Python code (especially boto3-heavy AWS scripts), the
recommended invocation is:

```bash
ulimit -v 8000000  # 8 GB; raises memory ceiling for symex
cbmc \
    --object-bits 12 \
    --no-unwinding-assertions --unwind 3 \
    --python-no-exception-checks \
    --python-required-kwarg-checks \
    --python-check-typeddict-fields \
    program.py
```

Each flag's role:

- `--object-bits 12`: raises CBMC's pointer-model addressed-object
  ceiling from `2^8 = 256` to `2^12 = 4096`. Necessary for benchmarks
  that materialise many heterogeneous dict entries (the front-end
  promotes those to per-entry `python_value_type` structs, each its
  own addressed object). Negligible overhead; should be the default
  for Python source.

- `--no-unwinding-assertions --unwind 3`: bound loops to 3 iterations
  without asserting that the bound is sufficient. Most boto3 idioms
  are linear in kwarg-key count; 3 is enough to cover typical
  per-method validation paths without blowing up symex.

- `--python-no-exception-checks`: skip the
  uncaught-exception-propagation property. Production Python code
  routinely uses `except Exception` for diagnostic wrapping; the
  default check fires on every `raise` not statically caught by a
  matching `except T`. For static API-misuse verification it is
  noise.

- `--python-required-kwarg-checks` (Tier 1B): emit a
  `required-kwarg` property at each call site whose stub-recorded
  TypedDict / `Unpack[Args]` schema declares a `Required[X]` kwarg
  the user didn't pass. Catches the typical "missing CreateApiKey
  parameter" class of bug.

- `--python-check-typeddict-fields`: emit a `type-error` property
  when a TypedDict field's value type doesn't match its declared
  type (e.g. passing `None` where the schema says `str`).

The following flag is **on by default** for `.py` source files —
listed here for visibility:

- `--python-check-any-arg-attrs`: at each call site where the
  callee's parameter is annotated `Any` and the caller's argument
  has a known concrete class type, emit `attribute-error`
  properties for `param.X(...)` references in the callee's body
  where `X` is not a method on the argument's class. Catches
  cross-function Any-erasure bugs (e.g. wrong boto3 client class
  flowing through an `Any`-typed parameter).

Optional flags worth considering:

- `--python-check-annotations`: emit `annotation-mismatch`
  properties at variable / parameter / return assignments where
  the annotation and assigned-value types are incompatible. Off
  by default — exposes 2 pre-existing CBMC core invariant
  violations (`boolbv_map.cpp:68`) on a couple of benchmarks. Use
  for individual files if the bench-level CBMC core blockers are
  not in your way.

- `--python-unbounded-ints --z3`: arbitrary-precision integers
  via SMT. Required for soundness on Python's `int` (which has no
  upper bound), but Z3-only.

- `--smt2 --cvc5`: route the back-end through CVC5 instead of
  CBMC's bit-blaster + MiniSat. Useful for cross-checking. The
  CVC5 path matches the default backend on the AWS Python
  benchmark suite (94.1 % pass rate). With `--slice-formula`
  (now default-on for `.py` source), CVC5's wall time on the
  AWS suite is within 6 % of the default backend and the
  `s3_backup_restore` outlier completes in 3 s (vs 64 s
  without slicing).

The following flag is **on by default** for `.py` source
files — listed here for visibility:

- `--slice-formula`: drop SMT assignments unrelated to a
  property's reachability before solving. Big win on cvc5
  outliers (`s3_backup_restore` 64 s → 3 s; full-suite cvc5
  total 338 s → 223 s). Smaller win on default (~2 % wall).
  Pass `--no-slice-formula` to disable. Previously a soundness
  blocker for the four `str-format-int-precision` /
  `fstring-*` regression tests because the slicer dropped
  CPROVER string-refinement intrinsic calls
  (`cprover_associate_array_to_pointer_func` etc.) whose SSA
  return-code symbol was unused. Fixed in commit 3c2a693177.

## Supported Python Features

### Types
- `int` (64-bit signed, or unbounded with `--python-unbounded-ints`)
- `float` (IEEE 754 double)
- `bool`
- `str` (bounded length, up to 256 characters)
- `list[T]` (bounded length, up to 64 elements)
- `tuple` (fixed-size, heterogeneous)
- `dict` (string-keyed, modeled as structs)
- Classes with instance attributes and methods

### Control Flow
- `if`/`elif`/`else`
- `while` with `break`/`continue`
- `for x in range(n)` and `for x in list`
- `try`/`except`/`finally`
- `with` statements
- `raise`

### Functions
- Definitions with type annotations
- Keyword arguments: `f(x=1, y=2)`
- Default parameters: `def f(x, y=10)`
- Recursive functions (bounded by `--unwind`)
- Lambda expressions: `double = lambda x: x * 2`

### Classes
- `__init__` with instance attributes
- Method calls with mutation (pointer-based model)
- Single inheritance
- `isinstance()` with inheritance chain
- Constructor calls in assignments, returns, and `with` statements

### Expressions
- Arithmetic: `+`, `-`, `*`, `//`, `%`, unary `-`
- Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=` (including chained)
- Boolean: `and`, `or`, `not`
- Bitwise: `|`, `&`, `^`, `<<`, `>>`
- Augmented assignment: `+=`, `-=`, `*=`, `//=`, `%=`, `|=`, etc.
- List comprehensions: `[x*2 for x in items]`
- Ternary: `x if cond else y`
- String operations: `len()`, indexing, comparison, concatenation
- Built-in functions: `int()`, `float()`, `bool()`, `abs()`, `min()`,
  `max()`, `print()`, `len()`, `isinstance()`

### Verification Primitives
```python
# Nondet values (symbolic, can be any value of the type)
x: int = nondet_int()
y: float = nondet_float()
b: bool = nondet_bool()

# Constrain nondet values
__CPROVER_assume(x > 0 and x < 100)

# Assertions (checked by CBMC)
assert x > 0
```

## Worked Example

Consider a binary search function:

```python
def binary_search(arr: list[int], target: int, size: int) -> int:
    low: int = 0
    high: int = size - 1
    while low <= high:
        mid: int = low + (high - low) // 2
        if arr[mid] == target:
            return mid
        elif arr[mid] < target:
            low = mid + 1
        else:
            high = mid - 1
    return -1
```

Save this as `search.py` and verify:

```bash
cbmc search.py --function binary_search --unwind 10 \
    --python-unbounded-ints --z3
```

CBMC will:
1. Generate nondet values for `arr`, `target`, and `size`
2. Unwind the while loop up to 10 times
3. Check for index-out-of-bounds on `arr[mid]`
4. Check for division by zero in `(high - low) // 2`
5. Check for integer overflow (if not using `--python-unbounded-ints`)

## Known Limitations

- **Dynamic typing**: Variables cannot change type (e.g., `x = 1; x = "hello"`)
- **Imports**: `import` statements resolve via CBMC's bundled Python
  library (`src/python/library/`) first, then `PYTHONPATH`. Modules
  without a bundled stub get a best-effort generic treatment.
- **Generators**: `yield` is emulated by eager-list accumulation; full
  PEP-380 semantics (send/throw/close) aren't modelled.
- **Decorators**: `@c_intrinsic(...)` and `@dataclass` are recognised;
  others are ignored.
- **`*args`/`**kwargs`**: Accepted in function signatures; forwarded
  calls pack keyword arguments into a dict param.
- **Slice expressions**: `lst[1:3]` is accepted but falls back to
  nondet; constant-index subscripts are precise.
- **String content tracking**: Length is tracked precisely via the
  refinement-string solver; content is tracked for constant strings
  and the `cprover_string_of_int_func` / `cprover_string_of_double_func`
  / `cprover_string_parse_int_func` / `cprover_string_concat_func`
  paths. `str.format()` and f-strings with format specs (`:d`,
  `:.2f`) or conversions (`!r`, `!s`) fall back to nondet.
- **Exception types**: `except` dispatches on exception-type hash; a
  single-string `__exception_payload` reaches `except T as e: str(e)`.
  Multi-arg exception payloads and `e.args` tuple are not yet tracked.

## Architecture Notes

### The tagged-union `python_value_type`

Values whose static type is a union (e.g. `Optional[T]`,
`Union[A, B]`, return values from functions with type-varying
branches) are carried at runtime as a struct with a
discriminator field:

```
python_value_type {
    int32_t  __tag;           // NONE=0 INT=1 FLOAT=2 BOOL=3
                              // STR=4 LIST=5 CLASS=6
    int64_t  __int_val;
    double   __float_val;
    int32_t  __bool_val;
    str*     __str_val;
    list*    __list_val;
    void*    __class_ptr;     // points to materialised class instance
}
```

Operations on tagged-union values dispatch on `__tag`:

- `isinstance(x, T)` for a primitive `T` compares `__tag`
  against the constant for `T`. For a user-defined `T`,
  reads `__class_tag` at offset 0 of `*__class_ptr` (int32)
  and OR-compares against `class_tag_ids[T]` plus every
  transitive subclass of `T`.

- `x.attr` and `x.attr = v` cast `__class_ptr` to the first
  `class_types[C]` whose struct has `attr`, then dereference.

- `x.method(...)` picks the class whose method table owns
  `method_name`. When multiple classes define the same
  `method_name`, dispatches virtually via `__class_tag`
  (if-chain of guarded calls, each assigning to a shared
  `__vdisp_N` tmp — PLR 3.3.2).

### The `@c_intrinsic` decorator

Functions in `src/python/library/` can be declared with
`@c_intrinsic("c_function_name")` to route the call through
a C-library model. Optional keyword arguments extend the
semantics:

- `fold="op"`: At parse time, if the call's arguments are
  all constants, evaluate in C++ using `std::op` and return
  the result as a `constant_exprt`. Supports one-arg
  (`sqrt`, `sin`, `log`, ...) and two-arg (`pow`, `atan2`,
  `hypot`, `fmod`, `copysign`, `remainder`) forms.
- `domain="pred_name"`: Emit a guarded `ValueError` at the
  call site if a named predicate rejects the argument.
- `range="range_name"`: Constrain the returned value to
  the named interval (e.g., `[-1, 1]` for `sin`).

### The library directory

`src/python/library/*.py` holds CBMC's built-in model of
the Python standard library plus a curated set of popular
third-party packages. Each file declares the type shape
and (where meaningful) implements the semantics in
Python. The frontend prefers bundled stubs over the
system CPython source; override with
`--python-use-stdlib-source` to force the latter.

The library currently covers: `math`, `cmath`, `struct`,
`itertools`, `functools`, `operator`, `string`, `re`,
`io`, `hashlib`, `base64`, `copy`, `enum`, `dataclasses`,
`abc`, `random`, `decimal`, `os` / `os.path`, `pathlib`,
`time`, `datetime`, `json`, `csv`, `logging`, `argparse`,
`textwrap`, `contextlib`, `warnings`, `traceback`,
`inspect`, `collections`, `heapq`, `bisect`, `signal`,
`errno`, `configparser`, `typing`, `urllib.parse`,
`subprocess`, `socket`, `threading`, `asyncio`, `yaml`,
`requests`.

### `--python-lazy-stubs`

Adds an optional processing mode where imported-module
function bodies are skipped — only type signatures are
registered. Calls return nondet via symex's no-body
fallback. Use when stub bodies dominate verification
cost (e.g., large third-party type stubs with embedded
assertions that are not relevant to the property being
verified).

### Correctness invariants enforced by PLR review

The Python-frontend work of the late 2025 sessions
added regression coverage and fixes for several PLR
(Python Language Reference) semantic rules:

- **§3.3.2 MRO / virtual dispatch**: tagged-union method
  calls pick the class whose `__class_tag` matches —
  not the first class in the symbol table.
- **§3.3.2.1 C3 linearization**: diamond inheritance
  `D(B, C)` where both B and C chain `super()` through
  A — every `__init__` body runs.
- **§3.3.1 ordering dunders**: `<`, `<=`, `>`, `>=` on
  class instances dispatch to `__lt__`/`__le__`/etc.
  with reflected-operand fallback.
- **§7.2.1 assignment**: the target list is bound only
  after the expression list on the right is fully
  evaluated. `a, b = b, a` snapshots the RHS into a
  `__unpack_N` tmp before mutating either LHS.
- **§7.12 `global`**: names bind to module scope.
- **§7.13 `nonlocal`**: names bind to the nearest
  enclosing function's scope. Separate from `global`;
  closure-capture pass skips `nonlocal`/`global` names
  so the binding doesn't get shadowed by a pass-by-value
  parameter.
- **§8.4 try/except/finally**: `raise` inside a try
  body is caught by the local `except`, not propagated.
  `else` runs iff no exception was raised in the try
  body (distinct from "except caught one").
- **§9.2.2 super()**: multi-level chains (C → B → A, D
  → C → B → A) inline every parent's body. A local
  buffer prevents the shared `pending_checks` vector
  from being clobbered across recursion.
- **§2.4.3 f-strings**: multi-part f-strings chain through
  `cprover_string_concat_func` so every part's content is
  visible to the string solver. `:0N` / `:>N` / `:<N` /
  `:^N` format specs emit length-constrained results.
- **§10.6 match/case**: patterns covered: `MatchValue`,
  `MatchSingleton`, `MatchOr`, `MatchAs` (with
  wildcard and binding), `MatchClass` (keyword
  patterns), `MatchSequence` (with `MatchStar`),
  `MatchMapping`. Guards with fall-through semantics.
- **PEP 572 walrus `:=`**: side-effects in a
  while-condition re-execute per iteration.
- **PEP 695 `type X = ...`**: accepted as a no-op.
- **§6.7 BinOp type safety**: arithmetic on
  incompatible operand types (int + list, str + dict)
  emits a nondet result instead of crashing — sound
  over-approximation of Python's runtime TypeError.

## Command-Line Options

| Option | Description |
|--------|-------------|
| `--function NAME` | Verify a specific function with nondet inputs |
| `--python-unbounded-ints` | Use mathematical integers (requires `--z3`) |
| `--python-max-string-length N` | Bound string length (default 256) |
| `--python-max-list-length N` | Bound list length (default 64) |
| `--python-no-body-check` | Suppress the missing-function-body check |
| `--python-strict-warnings` | Raise over-approximation log messages to warning level |
| `--python-use-stdlib-source` | Skip CBMC's bundled stubs; resolve via system CPython |
| `--python-smt-strings` | Represent `str` with the native SMT-LIB String sort instead of refinement strings (requires `--cvc5`/`--z3`) |
| `--python-lazy-stubs` | Import module signatures only; skip stub bodies |
| `--unwind N` | Bound loop/recursion unwinding to N iterations |
| `--z3` | Use Z3 SMT solver (required for unbounded ints) |
| `--cvc5` | Use CVC5 SMT solver |
| `--trace` | Show counterexample trace on failure |
| `--show-parse-tree` | Show the Python AST as JSON |
| `--show-properties` | List all generated properties |

## Developer tooling

- **`scripts/python_fuzzer.py`** — random-program
  harness. Generates small Python programs within a
  bounded grammar (arithmetic, comparison, IfExp,
  list/dict access, try/except, while, assignments)
  and runs `cbmc` on each, flagging invariant
  violations / segfaults / solver errors / timeouts.
  Example:

    ```
    scripts/python_fuzzer.py --count 500 --seed 1 \\
        --save-failures /tmp/cbmc-fuzz
    ```

  Not a correctness oracle — the generated programs
  have no expected results. Use as a stability check
  after frontend changes.

- **`scripts/profile_cbmc.py`** — flamegraph profiler
  (requires `perf_event_paranoid=-1`). See its `--help`
  for `--auto`, `--auto-large`, `--auto-csmith`, and
  `--diff REF_A REF_B` modes.

- **`.github/workflows/python-regression.yaml`** — CI
  runs the 346 CORE regression tests on every PR to
  `develop`.

## Worked examples

### Union-return + isinstance

```python
class Dog:
    def sound(self) -> int: return 1
class Cat:
    def sound(self) -> int: return 2

def pick(b: bool):
    return Dog() if b else Cat()

x = pick(True)
# x is tagged-union; method call dispatches via __class_tag
assert x.sound() == 1
```

### Match/case over a union

```python
def classify(v):
    match v:
        case 0: return "zero"
        case n if n > 0: return "positive"
        case _: return "negative"
```

### Multi-inheritance diamond

```python
class A:
    def __init__(self): self.a = 1
class B(A):
    def __init__(self): super().__init__(); self.b = 2
class C(A):
    def __init__(self): super().__init__(); self.c = 3
class D(B, C):
    def __init__(self): super().__init__(); self.d = 4

d = D()
# C3 MRO: [D, B, C, A]. All four fields set.
assert d.a == 1 and d.b == 2 and d.c == 3 and d.d == 4
```

## Requirements

- CBMC built with the Python front-end
- Python 3 interpreter on PATH (used to parse `.py` files via `ast` module)
- Z3 (optional, for `--python-unbounded-ints`)
