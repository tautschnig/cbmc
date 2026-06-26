# --python-check-annotations: an annotation the frontend cannot model
# precisely (a `range`, a bare `tuple`, an unknown forward reference)
# lowers to python_value (Any / top), which is compatible with every
# value type. So assigning the natural value to such a binding must NOT
# raise a spurious annotation-mismatch. (Regression for the int-fallback
# collision: `range`/`tuple`/unknown used to default to int and flag.)
from typing import Any


def f() -> "SomeUndeclaredType":  # unknown forward ref -> Any
    return 123


r: range = range(4)          # bare range  -> Any, value is a range
t: tuple = (1, 2)            # bare tuple  -> Any, value is a tuple
v = f()                      # unknown return -> Any

# Any is compatible with a concrete re-annotation in either direction.
a: Any = r
n: int = 5                   # genuine int annotation still works

assert n == 5
