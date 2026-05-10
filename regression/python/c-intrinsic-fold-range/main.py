# @c_intrinsic's range= keyword: when a math function is called
# with a symbolic argument, the returned value is a nondet
# constrained by the declared range predicate. The tests below
# exercise sin / cos / sqrt / exp via 'from math import ...',
# which now routes through the decorator (the pre-retirement
# ad-hoc path did this via imported_math_funcs; after retirement,
# library/math.py's @c_intrinsic annotations drive it).


def nondet_float() -> float: ...


# sin / cos: range = [-1, 1] regardless of the argument.
from math import sin, cos

x1 = sin(nondet_float())
assert x1 >= -1.0
assert x1 <= 1.0

x2 = cos(nondet_float())
assert x2 >= -1.0
assert x2 <= 1.0


# exp: range > 0.
from math import exp

x3 = exp(nondet_float())
assert x3 > 0.0
