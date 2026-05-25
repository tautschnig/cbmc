# PLR: lambda parameter type widening. Without an annotation,
# the lambda's parameter type was hard-coded to int. For
# 'lambda a: a + 1.1', this caused the parameter to be int
# and the float argument 1.9 to be truncated to 1, returning
# 1+1.1=2.1 instead of 1.9+1.1=3.0. The assertion
# 'x(1.9) < 3.0' silently passed because 2.1 < 3.0 is true.
#
# Fix: scan the lambda body for binary operations involving
# float constants. If the body uses any float literal, widen
# the parameter type to double. Otherwise keep int.

def lambda_with_float_op() -> None:
    f = lambda a: a + 1.1
    # 1.9 + 1.1 = 3.0 (not 2.1 from int-truncated 1+1.1)
    assert f(1.9) == 3.0


def lambda_int_only() -> None:
    f = lambda x: x + 1
    # No float constant in body — int param keeps full int range.
    assert f(5) == 6


def lambda_explicit_int() -> None:
    f = lambda x: x * 2
    assert f(7) == 14


lambda_with_float_op()
lambda_int_only()
lambda_explicit_int()
