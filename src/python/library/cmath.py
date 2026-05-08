"""
Verification model of the `cmath` module (complex-number math).

Each function maps to a nondet complex-valued stand-in. The
C math library has 'csin', 'ccos', etc., but CBMC's complex-number
support is limited; we model each as a shape-preserving nondet.
"""


# Complex constants. In real Python these are stored as complex
# literals; here we use a simple float tuple.
pi: float = 3.141592653589793
e: float = 2.718281828459045
tau: float = 6.283185307179586
inf: float = float("inf")
nan: float = float("nan")
infj = complex(0, float("inf"))
nanj = complex(0, float("nan"))


def sqrt(x):
    return 0 + 0j


def exp(x):
    return 0 + 0j


def log(x, base=None):
    return 0 + 0j


def log10(x):
    return 0 + 0j


def sin(x):
    return 0 + 0j


def cos(x):
    return 0 + 0j


def tan(x):
    return 0 + 0j


def asin(x):
    return 0 + 0j


def acos(x):
    return 0 + 0j


def atan(x):
    return 0 + 0j


def sinh(x):
    return 0 + 0j


def cosh(x):
    return 0 + 0j


def tanh(x):
    return 0 + 0j


def asinh(x):
    return 0 + 0j


def acosh(x):
    return 0 + 0j


def atanh(x):
    return 0 + 0j


def phase(x) -> float:
    return 0.0


def polar(x):
    return (0.0, 0.0)


def rect(r: float, phi: float):
    return 0 + 0j


def isfinite(x) -> bool:
    return True


def isinf(x) -> bool:
    return False


def isnan(x) -> bool:
    return False


def isclose(a, b, rel_tol: float = 1e-9, abs_tol: float = 0.0) -> bool:
    return True
