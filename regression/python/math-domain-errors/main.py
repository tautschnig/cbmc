# PLR: library reference for the math module specifies that
# domain-violating inputs raise ValueError.
#
# Three cases are exercised:
#
#   Case 1 — constant in-domain: the call folds to the exact value
#            at parse time.
#   Case 2 — constant out-of-domain: the call raises ValueError
#            definitely; a try/except catches it.
#   Case 3 — non-constant out-of-domain: the frontend emits a
#            guarded ValueError; the except path runs.

import math


# Case 1: constant in-domain — exact fold.
assert math.sqrt(4.0) == 2.0
assert math.log(1.0) == 0.0
assert math.acos(1.0) == 0.0
assert math.asin(0.0) == 0.0


# Case 2: constant out-of-domain — caught by try/except.
def case2_sqrt_negative() -> float:
    y: float = 99.0
    try:
        y = math.sqrt(-1.0)
    except ValueError:
        y = 99.0
    return y


assert case2_sqrt_negative() == 99.0


def case2_log_zero() -> float:
    y: float = 99.0
    try:
        y = math.log(0.0)
    except ValueError:
        y = 99.0
    return y


assert case2_log_zero() == 99.0


def case2_asin_out_of_range() -> float:
    y: float = 99.0
    try:
        y = math.asin(2.0)
    except ValueError:
        y = 99.0
    return y


assert case2_asin_out_of_range() == 99.0


# Case 3: non-constant — guarded ValueError. A try/except must
# catch the raise even when the caller can't predict the input.
def safe_sqrt(x: float) -> float:
    result: float = 0.0
    try:
        result = math.sqrt(x)
    except ValueError:
        result = 0.0
    return result


# For any input, the return is non-negative: either sqrt(x) >= 0
# (C library constraint) or 0.0 (except branch).
assert safe_sqrt(-0.5) >= 0.0
assert safe_sqrt(4.0) >= 0.0
