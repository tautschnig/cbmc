"""
Verification model of the `math` module.

This file declares the math module's public surface using
``@c_intrinsic`` annotations so stub authors and readers have a
single place to look up 'what do we support?'. The *implementation*
of each call, however, currently lives in the frontend's
handwritten path in ``python_converter.cpp`` — that path is the
fast-path because it performs parse-time constant folding
(``math.sin(0.5)`` → 0.479...), which the plain ``@c_intrinsic``
route cannot do today.

A future iteration will teach the ``@c_intrinsic`` dispatch to
constant-fold for a known set of math functions and at that point
this file becomes the sole source of truth; the handwritten path
will be deleted.

For now, the file is primarily a readable specification plus a
place to record:
  - which functions are supported at all (see the decorator list)
  - where the domain predicate lives (see
    python_convertert::math_function_domain)
  - the module-level constants (which are already used directly
    via the frontend's attribute-level handling; re-stating them
    here makes ``from math import pi`` work after the full
    migration).
"""

from __cbmc__ import c_intrinsic


# ---------------------------------------------------------------
# Constants (IEEE-754 doubles). These mirror CPython exactly.
# ---------------------------------------------------------------
pi: float = 3.141592653589793
e: float = 2.718281828459045
tau: float = 6.283185307179586
inf: float = float("inf")
nan: float = float("nan")


# ---------------------------------------------------------------
# Power and logarithmic. Domain-restricted entries are also handled
# by python_convertert::math_function_domain (Option 4).
# ---------------------------------------------------------------
@c_intrinsic("sqrt", fold="sqrt", domain="nonneg", range="nonneg")
def sqrt(x: float) -> float: ...


@c_intrinsic("cbrt", fold="cbrt")
def cbrt(x: float) -> float: ...


@c_intrinsic("exp", fold="exp", range="positive")
def exp(x: float) -> float: ...


@c_intrinsic("exp2", fold="exp2", range="positive")
def exp2(x: float) -> float: ...


@c_intrinsic("expm1", fold="expm1", range="positive")
def expm1(x: float) -> float: ...


@c_intrinsic("log", fold="log", domain="positive")
def log(x: float) -> float: ...


@c_intrinsic("log2", fold="log2", domain="positive")
def log2(x: float) -> float: ...


@c_intrinsic("log10", fold="log10", domain="positive")
def log10(x: float) -> float: ...


@c_intrinsic("log1p", fold="log1p", domain="gt_neg_one")
def log1p(x: float) -> float: ...


@c_intrinsic("pow", fold="pow")
def pow(x: float, y: float) -> float: ...


# ---------------------------------------------------------------
# Trigonometric and hyperbolic.
# ---------------------------------------------------------------
@c_intrinsic("sin", fold="sin", range="bound_pm_1")
def sin(x: float) -> float: ...


@c_intrinsic("cos", fold="cos", range="bound_pm_1")
def cos(x: float) -> float: ...


@c_intrinsic("tan", fold="tan")
def tan(x: float) -> float: ...


@c_intrinsic("asin", fold="asin", domain="abs_le_1")
def asin(x: float) -> float: ...


@c_intrinsic("acos", fold="acos", domain="abs_le_1")
def acos(x: float) -> float: ...


@c_intrinsic("atan", fold="atan")
def atan(x: float) -> float: ...


@c_intrinsic("atan2", fold="atan2")
def atan2(y: float, x: float) -> float: ...


@c_intrinsic("sinh", fold="sinh")
def sinh(x: float) -> float: ...


@c_intrinsic("cosh", fold="cosh")
def cosh(x: float) -> float: ...


@c_intrinsic("tanh", fold="tanh")
def tanh(x: float) -> float: ...


@c_intrinsic("asinh", fold="asinh")
def asinh(x: float) -> float: ...


@c_intrinsic("acosh", fold="acosh", domain="ge_1")
def acosh(x: float) -> float: ...


@c_intrinsic("atanh", fold="atanh", domain="abs_lt_1")
def atanh(x: float) -> float: ...


@c_intrinsic("hypot", fold="hypot")
def hypot(x: float, y: float) -> float: ...


# ---------------------------------------------------------------
# Rounding and sign. The frontend's inline path has exact models
# for ceil/floor/trunc/fabs/copysign; the @c_intrinsic declarations
# here describe the externally-visible contract.
# ---------------------------------------------------------------
@c_intrinsic("ceil", fold="ceil")
def ceil(x: float) -> int: ...


@c_intrinsic("floor", fold="floor")
def floor(x: float) -> int: ...


@c_intrinsic("trunc", fold="trunc")
def trunc(x: float) -> int: ...


@c_intrinsic("fabs", fold="fabs")
def fabs(x: float) -> float: ...


@c_intrinsic("copysign", fold="copysign")
def copysign(x: float, y: float) -> float: ...


@c_intrinsic("fmod", fold="fmod")
def fmod(x: float, y: float) -> float: ...


@c_intrinsic("remainder", fold="remainder")
def remainder(x: float, y: float) -> float: ...


@c_intrinsic("fdim")
def fdim(x: float, y: float) -> float: ...


@c_intrinsic("fmax")
def fmax(x: float, y: float) -> float: ...


@c_intrinsic("fmin")
def fmin(x: float, y: float) -> float: ...


# ---------------------------------------------------------------
# Special functions.
# ---------------------------------------------------------------
@c_intrinsic("erf", fold="erf")
def erf(x: float) -> float: ...


@c_intrinsic("erfc", fold="erfc")
def erfc(x: float) -> float: ...


@c_intrinsic("lgamma", fold="lgamma")
def lgamma(x: float) -> float: ...


@c_intrinsic("tgamma")
def tgamma(x: float) -> float: ...


# ---------------------------------------------------------------
# Degree / radian conversion. These have simple closed forms and
# are folded by the inline path when the argument is constant.
# ---------------------------------------------------------------
def degrees(x: float) -> float:
    return x * 180.0 / pi


def radians(x: float) -> float:
    return x * pi / 180.0


# ---------------------------------------------------------------
# Float classification. Exact models live in the frontend's inline
# path.
# ---------------------------------------------------------------
def isnan(x: float) -> bool:
    return False


def isinf(x: float) -> bool:
    return False


def isfinite(x: float) -> bool:
    return True


def isclose(
    a: float,
    b: float,
    rel_tol: float = 1e-9,
    abs_tol: float = 0.0,
) -> bool:
    return True


# ---------------------------------------------------------------
# Integer-valued. The frontend's inline path handles factorial/comb
# with non-negative nondet returns.
# ---------------------------------------------------------------
def factorial(n: int) -> int:
    if n < 0:
        raise ValueError("factorial() not defined for negative values")
    return 0


def comb(n: int, k: int) -> int:
    if n < 0 or k < 0:
        raise ValueError("n and k must be non-negative integers")
    return 0


def perm(n: int, k: int) -> int:
    if n < 0 or k < 0:
        raise ValueError("n and k must be non-negative integers")
    return 0


def gcd(*integers) -> int:
    return 0


def lcm(*integers) -> int:
    return 0


def isqrt(n: int) -> int:
    if n < 0:
        raise ValueError("isqrt() argument must be non-negative")
    return 0
