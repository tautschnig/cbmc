# Known limitations found via hypothesmith fuzzing

This file records semantic-modelling gaps the
hypothesmith --unrestricted fuzzer surfaces. Each entry
links the failing fuzz program shape to the underlying
frontend issue and points at the area of code that would
need to change to close it.

## Nested function name conflict (1 failure as of wave 26)

**Symptom**: when two sibling functions each contain a
nested function with the same name, the second
definition overwrites the first in the symbol table.

```python
def block_0():
    def f(*args):
        return sum(args)
    assert f(1, 2, 3) == 6      # FAILS — uses block_3's f

def block_3():
    def f(**kwargs):
        return kwargs.get("x", 0)
    assert f(x=42) == 42

block_0()
block_3()
```

**Root cause**: `python_converter_defs.cpp` registers
nested functions with the bare name as their symbol id
(`python::f`), so two sibling-scope `def f(...)` collide.

**Why a quick fix is risky**: qualifying nested function
names with their enclosing scope chain
(`python::block_0::f` etc.) requires synchronised
updates in:

- `convert_function_def` symbol-id and parameter-id
  construction
- `convert_call` callee resolution to walk the scope
  chain
- the closure-capture mechanism in
  `python_converter_defs.cpp` (where the
  `closure_captures` map is keyed by the nested
  function's id)
- the decorator path that looks up `wrapper` by
  convention
- `qualify_name` and the `function_aliases` lookup
- snapshot/restore for branch-local maps

A first attempt (May 2026) qualified the symbol id and
walked scopes at call sites, but broke the decorator
pattern `@my_dec; def f(...)` because the decorator
path searches for the wrapper symbol by hardcoded id
patterns. Reverted.

**Proper fix**: introduce a single nested-function
naming convention used throughout
`python_converter_defs.cpp`, `python_converter_call.cpp`
and `python_converter_lambda.cpp`, then update
`function_aliases`, `closure_captures`, the decorator
wrapper lookup and `qualify_name` to use it
consistently. Should be done as one focused change
with the regression suite + hypothesmith all clean.
