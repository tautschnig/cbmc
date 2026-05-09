# @c_intrinsic(..., fold=...) — parse-time constant folding on
# Python calls to C library functions. This test doesn't import
# math, so only the decorator's fold path is exercised (not the
# ad-hoc math handling).

def c_intrinsic(name, fold=None):
    def deco(fn):
        return fn
    return deco


@c_intrinsic("sqrt", fold="sqrt")
def sqrt(x: float) -> float: ...


@c_intrinsic("tgamma", fold="gamma")
def gamma(x: float) -> float: ...


@c_intrinsic("erf", fold="erf")
def erf(x: float) -> float: ...


# sqrt(9) = 3
assert sqrt(9.0) == 3.0
# sqrt(0) = 0
assert sqrt(0.0) == 0.0
# gamma(1) = 1, gamma(5) = 24
assert gamma(1.0) == 1.0
assert gamma(5.0) == 24.0
# erf(0) = 0
assert erf(0.0) == 0.0
