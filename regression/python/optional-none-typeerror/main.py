# Differential unsoundness witness §8 (cbmc-py-differential):
# Optional[T] collapses to T, so a parameter typed Optional[int] is
# modeled as a plain int that can never be None -- the None path /
# TypeError is unreachable. CPython: f(None) -> TypeError (int + NoneType).
# cbmc-py currently reports SUCCESSFUL (false negative). KNOWNBUG until
# Optional[int] is lowered to the tagged union with a NONE tag.
from typing import Optional
def f(x: Optional[int]) -> int:
    return x + 1
f(None)
assert True
