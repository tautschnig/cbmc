\file
# Authoring stubs for `src/python/library/`

This document is a practical guide to writing a new model for
CBMC's Python front-end. It complements the architectural plan in
`doc/python-frontend-plans.md` (§6 modules) by answering: *how do I add
`yourmodule.py` to the library?*

## File layout

Mirror CPython's layout exactly.

| To model          | File                                 |
|-------------------|--------------------------------------|
| `import foo`      | `src/python/library/foo.py`          |
| `from foo.bar …`  | `src/python/library/foo/bar.py`      |
| `import foo.bar`  | `src/python/library/foo/bar.py` *(and usually `src/python/library/foo/__init__.py`)* |

See existing examples: `urllib/parse.py`, `collections/__init__.py`,
`datetime.py`, `json/__init__.py`, `re/__init__.py`, `errno.py`,
`signal.py`, `cmath.py`, `os/__init__.py`, `os/path.py`, `time.py`.

## What a stub file looks like

A stub is **real Python**: it must parse with CPython
(`python3 -c "import ast; ast.parse(open('yourfile').read())"`),
and it should be a valid module (so tests and tools that
incidentally import it don't blow up). The front-end extracts
meaning by pattern-matching on the AST, not by running the code.

### Skeleton

```python
"""
One-line description of the module this file models.

Longer description: what entry points are covered, what is
intentionally nondet, what is out of scope. Cross-reference the
CPython docs if helpful.
"""

from __cbmc__ import c_intrinsic  # optional; only if using @c_intrinsic


# Module-level constants (ordinary Python assignments work).
SOME_FLAG: int = 1
DEFAULT_NAME: str = "unknown"


class PublicClass:
    """Shape-compatible stand-in for the real class."""

    # Attribute annotations define the struct components.
    name: str
    count: int

    def __init__(self, name: str = "", count: int = 0):
        self.name = name
        self.count = count

    def do_thing(self) -> int:
        # Nondet body: the enclosing front-end will let callers
        # explore all int-valued results.
        return 0


def public_function(arg: str) -> str:
    # An explicit, conservative overapproximation: empty string.
    # The caller still sees a `str`-typed result.
    return ""
```

## Typing conventions

Annotate *every* parameter and *every* return type. Unannotated
parameters fall back to `python_value_type()` (a tagged union)
which is unnecessarily imprecise for stubs.

Use plain `int`, `float`, `bool`, `str`, `list`, `dict`. Avoid
`typing.*` here: the stubs should be ingestable even when the
`typing` model isn't loaded.

## Choosing a body

Pick the simplest body consistent with the function's contract:

* **Constant** (`return ""`, `return 0`, `return False`): the
  front-end constant-folds; assertions against known values verify
  precisely. Prefer this when the real function's return is
  stylised (e.g. `urllib.parse.quote` returning `""` is fine for
  most verification purposes).
* **Nondet via `return nondet_bool()` (or similar primitive)**:
  when you want both "true" and "false" paths explored. Useful for
  predicates like `re.match` (matches or doesn't). See below for
  the primitive list.
* **`return None`**: only when the real CPython call really does
  always return `None`.
* **`...`** (ellipsis) as the body: use it with `@c_intrinsic`
  — the body is never executed, the C function is called instead.

## Routing to a C function: `@c_intrinsic`

If the function has the same semantics as one in CBMC's ansi-c
library — `math.sin`, `math.sqrt`, etc. — route calls there:

```python
from __cbmc__ import c_intrinsic

@c_intrinsic("sqrt")
def sqrt(x: float) -> float:
    ...
```

Requirements:

1. The named C function must exist in `src/ansi-c/library/*.c`
   (CBMC's link-to-library pass will bring its body in).
2. The Python signature's parameter and return types must be
   **C-representable primitives** — currently `int`/`float`/`bool`.
   Signatures involving Python `str` or `list` do **not** route
   correctly because Python's refined-string struct and list
   struct don't match C's `char *` or pointer representations.
   Use a plain nondet stub instead.
3. The function body must be exactly `...` or `pass`. The decorator
   tells the front-end to ignore the body.

Regression coverage: `regression/python/c-intrinsic-decorator/`.

## Constants

Module-level constants are plain Python assignments:

```python
MINYEAR: int = 1
DEFAULT_SEP: str = "/"
```

They are evaluated at parse time by the front-end. Keep
expressions on the right-hand side simple (literals, arithmetic on
literals, tuple/list/dict literals of literals). Don't call
functions to compute constant values.

## Classes

Declare attribute types at class scope; define an `__init__`
that assigns them. This lets the front-end build a proper struct
type for instances. See `urllib/parse.py`'s `ParseResult` or
`datetime.py`'s `datetime`.

Classmethods that would normally call `cls()` to construct a new
instance must currently return an explicit literal of the class
— the front-end doesn't yet recognise `cls()` as a constructor
call:

```python
@classmethod
def today(cls):
    return date()          # not: return cls()
```

## Interaction with user code

Stubs are discovered and loaded via the same `resolve_module`
machinery as user imports. User code that does
`from yourmodule import foo` sees the stub's `foo` directly; user
code that does `import yourmodule` sees `yourmodule` as a module
symbol and accesses attributes lazily. Both forms work; the
`from … import …` form tends to verify more precisely because
each imported name is bound with its annotated type.

A user can bypass the library with `--python-use-stdlib-source`
and resolve imports only through `PYTHONPATH`/the system CPython
installation. This is useful for debugging discrepancies between
a stub and the real module.

## Testing a new stub

1. Write `regression/python/python-library-<modulename>/main.py`
   that `from <modulename> import …`s every symbol the stub
   exposes. Include at least one `assert True` so the test has a
   property to check.
2. `regression/python/python-library-<modulename>/test.desc` with
   positive-match lines for each expected
   ``Resolving import: <modulename> → .*library/<modulename>…``.
   Negative-match lines should include
   ``^no body for callee`` and ``^Unknown method|function``.
3. Run locally:
   `cd regression/python && perl ../test.pl -e -p -c "$(pwd)/../../build/bin/cbmc" -C python-library-<modulename>`.

Once the regression test passes, the new module is picked up by
the `integration/python-stdlib/` test automatically the next time
the baseline is refreshed — update it with
`./run_stdlib_parse_test.sh --update-baseline` and commit the diff.

## Rules of thumb

* Err on the side of smaller stubs. It's easier to add a method
  later than to diagnose an unsound stub.
* Prefer shape-preserving nondet over clever partial models. A
  nondet result is a sound over-approximation; a partial model
  (e.g. "return 0 when input is empty, nondet otherwise") is easy
  to get subtly wrong.
* Don't copy-paste from CPython. The real source is often
  performance-tuned in ways that don't help verification — and
  writing your own stub is usually faster than trying to simplify
  CPython's.
* Don't model C-extension modules (`_datetime`, `_re`, `_json`,
  `_struct`, ...) directly. Wrap them with the pure-Python form
  of the module (`datetime`, `re`, `json`, ...) and let that
  model handle the public surface.
