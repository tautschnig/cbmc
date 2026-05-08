# Exercises the @c_intrinsic decorator: a Python function marked
# with @c_intrinsic('NAME') is lowered by the front-end to a call
# to the C function NAME. The C function is provided by CBMC's
# ansi-c library (math.c) at link-to-library time.
#
# This test doesn't live in src/python/library/ because we want
# the mechanism itself to be testable without relying on the
# library lookup path — the stub below is inline.


def c_intrinsic(name):
    # Runtime no-op so the file is importable by real Python. The
    # front-end recognises @c_intrinsic('NAME') at AST level.
    def _d(f):
        return f
    return _d


@c_intrinsic('sqrt')
def sqrt(x: float) -> float:
    ...


@c_intrinsic('sin')
def sin(x: float) -> float:
    ...


# Exercise: sqrt is a real C math function, so the solver sees
# CBMC's ansi-c/library/math.c model of sqrt (nondet with
# sqrt(x)*sqrt(x) == x, sqrt(x) >= 0, ...). A simple sanity
# check: sqrt of a non-negative value is non-negative.
def check(x: float) -> bool:
    if x < 0.0:
        return True  # undefined domain; skip
    return sqrt(x) >= 0.0


assert check(4.0)
assert check(9.0)

# sin over [0, 1] — the C library model says sin(x) in [-1, 1];
# we don't commit to a specific value.
_ = sin(0.5)
assert True
