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
- **Imports**: `import` statements are silently ignored; imported names
  are treated as unknown (nondet return values with a warning)
- **Generators**: Generator expressions and `yield` are not supported
- **Decorators**: Not supported
- **`*args`/`**kwargs`**: Not supported
- **Slice expressions**: `lst[1:3]` not supported
- **String content tracking**: String concatenation tracks length but not
  character content
- **Exception types**: `except` catches all exceptions regardless of type

## Command-Line Options

| Option | Description |
|--------|-------------|
| `--function NAME` | Verify a specific function with nondet inputs |
| `--python-unbounded-ints` | Use mathematical integers (requires `--z3`) |
| `--unwind N` | Bound loop/recursion unwinding to N iterations |
| `--z3` | Use Z3 SMT solver (required for unbounded ints) |
| `--trace` | Show counterexample trace on failure |
| `--show-parse-tree` | Show the Python AST as JSON |
| `--show-properties` | List all generated properties |

## Requirements

- CBMC built with the Python front-end
- Python 3 interpreter on PATH (used to parse `.py` files via `ast` module)
- Z3 (optional, for `--python-unbounded-ints`)
