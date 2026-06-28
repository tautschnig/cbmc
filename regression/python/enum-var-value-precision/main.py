# Precision: `.value` on a VARIABLE holding an enum member (not just the static
# `EnumClass.MEMBER.value` form) resolves to the member's value, instead of a
# nondet attribute read.
from enum import Enum


class Color(Enum):
    R = "red"
    G = "green"


class Num(Enum):
    A = 1
    B = 2


c = Color.R
assert c.value == "red"
n = Num.B
assert n.value == 2
