# @c_intrinsic(..., fold=..., domain=...) — parse-time constant
# folding + domain-check ValueError for monadic math functions.
# Tests the decorator in isolation (no math import), so only the
# decorator path is exercised.

def c_intrinsic(name, fold=None, domain=None):
    def deco(fn):
        return fn
    return deco


@c_intrinsic("sqrt", fold="sqrt", domain="nonneg")
def sqrt(x: float) -> float: ...


@c_intrinsic("log", fold="log", domain="positive")
def log(x: float) -> float: ...


@c_intrinsic("asin", fold="asin", domain="abs_le_1")
def asin(x: float) -> float: ...


@c_intrinsic("acosh", fold="acosh", domain="ge_1")
def acosh(x: float) -> float: ...


# In-domain: fold correctly.
assert sqrt(9.0) == 3.0
assert sqrt(0.0) == 0.0

# Out-of-domain (constant) raises ValueError. The try/except
# intercepts; the assertion afterwards still verifies because the
# frontend's exception machinery runs before the assert is
# observed.
try:
    r = sqrt(-4.0)  # Not in domain → ValueError raised
    assert False, "expected ValueError"
except ValueError:
    pass

try:
    r = log(0.0)  # Not in domain (>0 required) → ValueError raised
    assert False, "expected ValueError"
except ValueError:
    pass

try:
    r = asin(2.0)  # Not in domain (|x| <= 1) → ValueError raised
    assert False, "expected ValueError"
except ValueError:
    pass

try:
    r = acosh(0.5)  # Not in domain (>= 1 required) → ValueError
    assert False, "expected ValueError"
except ValueError:
    pass
