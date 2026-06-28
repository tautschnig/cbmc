# Soundness: a variable reassigned across members of a HETEROGENEOUS enum carries
# the new member's value type, so a use at the wrong type is caught. After
# s = S.B, s.value is "x" (str); "x" - 1 raises TypeError in CPython.
from enum import Enum


class S(Enum):
    A = 1
    B = "x"


s = S.A
s = S.B
r = s.value - 1   # CPython: TypeError (str - int)
