# KNOWNBUG (false proof): the call-argument tag obligation fires for FREE
# functions (an Any-typed str bound to an int-annotated param is caught) but NOT
# for METHOD calls. CPython: n + 1 with n = "x" raises TypeError.
from typing import Any

class Foo:
    def bar(self, n: int) -> int:
        return n + 1

def get() -> Any:
    return "x"

f = Foo()
v: Any = get()
r = f.bar(v)     # str bound to int param; CPython TypeError at n + 1
assert True      # unreachable in CPython
