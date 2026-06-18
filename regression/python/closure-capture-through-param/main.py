# Fat-closure phase 2 (doc/python-frontend-fat-closure-plan.md):
# capture-through-param. A capturing closure passed by value to a
# higher-order function and called there now observes its captures,
# because the closure is boxed into a CLOSURE python_value carrying a
# per-instance capture record. Param captures are bound from the
# factory's call arguments (sound across repeated calls to the same
# factory); local captures from running the factory body.
#
# Ordering note: the higher-order callee's runtime dispatch enumerates
# the closures registered when its body is converted, so factories must
# be defined before the higher-order function that calls them. Reverse
# ordering degrades to sound nondet (a precision limit, not a false
# proof) until the function-pointer calling convention lands.


def make(n):
    return lambda: n


def make_local():
    x = 5
    return lambda: x


def call(f):
    return f()


# Capture through a parameter, called inside the HOF.
assert call(make(7)) == 7

# Multiple closures from the SAME factory stay independent (per-instance
# capture records) — the soundness-critical property.
a = call(make(7))
b = call(make(8))
assert a == 7
assert b == 8
# A wrong / aliased value is not provable (no false proof).
assert b != 7

# Local capture through a parameter.
assert call(make_local()) == 5
