# PLR §6.5: power operator with fractional / non-integer
# exponent. The constant-fold path used to round-trip through
# the IEEE-formatted ANSI string (default ~6-7 digits), which
# silently truncated values like 1/3 to a 6-digit double and
# made 8 ** (1/3) fold to 1.999999 instead of 2.0.

# Constant integer base, fractional exponent
assert 4 ** 0.5 == 2.0
assert 8 ** (1 / 3) == 2.0
assert 27 ** (1 / 3) == 3.0
assert 16 ** 0.25 == 2.0
assert 32 ** (1 / 5) == 2.0
# CPython: 27 ** (2/3) is 8.999999999999998, NOT 9.0 (the 2/3
# double is inexact; pow cannot recover). Assert the true value's
# bracket -- the original `== 9.0` expectation was refuted by
# CPython on this host.
assert 8.99 < 27 ** (2 / 3) < 9.0

# Negative exponent
assert 8 ** (-1 / 3) == 0.5

# Float base
assert 2.5 ** 2.5 > 9.88
assert 2.5 ** 2.5 < 9.89

# NaN / infinity propagation through std::pow
import math
nan = float("nan")
assert math.isnan(2 ** nan)
assert math.isnan(nan ** nan)
assert (1.0 ** nan) == 1.0
