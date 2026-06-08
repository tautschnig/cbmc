import math

# abs(complex) is modelled as a nondet sqrt constrained by the magnitude.
# The IEEE boundaries must still be pinned exactly: a bare result*result ==
# real^2 + imag^2 underflows to 0 and overflows to inf, so these would fail.
assert abs(complex(0, 0)) == 0.0
assert abs(complex(3, 4)) == 5.0
assert math.isinf(abs(complex(float("inf"), 1.0)))
assert math.isnan(abs(complex(float("nan"), 1.0)))
