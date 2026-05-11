# Verifying Python Programs with CBMC

CBMC includes a Python front-end that can verify Python 3 programs using
bounded model checking. This guide explains how to use it.

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
- **§7.2.1 assignment**: the target list is bound only
  after the expression list on the right is fully
  evaluated. `a, b = b, a` snapshots the RHS into a
  `__unpack_N` tmp before mutating either LHS.
- **§9.2.2 super()**: multi-level chains (C → B → A, D
  → C → B → A) inline every parent's body. A local
  buffer prevents the shared `pending_checks` vector
  from being clobbered across recursion.
- **§2.4.3 f-strings**: multi-part f-strings chain through
  `cprover_string_concat_func` so every part's content is
  visible to the string solver.

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
| `--python-smt-strings` | Use the SMT string theory instead of refinement strings |
| `--python-lazy-stubs` | Import module signatures only; skip stub bodies |
| `--unwind N` | Bound loop/recursion unwinding to N iterations |
| `--z3` | Use Z3 SMT solver (required for unbounded ints) |
| `--cvc5` | Use CVC5 SMT solver |
| `--trace` | Show counterexample trace on failure |
| `--show-parse-tree` | Show the Python AST as JSON |
| `--show-properties` | List all generated properties |

## Requirements

- CBMC built with the Python front-end
- Python 3 interpreter on PATH (used to parse `.py` files via `ast` module)
- Z3 (optional, for `--python-unbounded-ints`)
