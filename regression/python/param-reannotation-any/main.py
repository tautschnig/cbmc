# A function PARAMETER re-annotated inside the body (`x: Any = ...`)
# must NOT have its symbol retyped in place: the parameter's type is the
# function's call contract, and mutating it makes every call site's
# argument ill-typed (symex "parameter type mismatch" -- the
# setup_cloudformation corpus crash). The body rebinds through a fresh
# versioned symbol instead; the runtime tag is preserved.
from typing import Any


def g(v: str) -> Any:
    return v


def f(x: str) -> bool:
    x: Any = g(x)
    return isinstance(x, str)


assert f("a")
