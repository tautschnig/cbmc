# PLR §8.7: a decorator wrapper with a different arity than the call. `w()` takes
# no args but the decorated name is called with one -> CPython TypeError. The
# frontend now redirects `f` through the wrapper and enforces the wrapper's
# signature at the call site. This witness also names the decorator parameter
# `f` (the same as the decorated function) to lock in the alias-collision guard:
# the `python::f -> wrapper` redirect must not be clobbered by the fn-param
# binding. Expected: VERIFICATION FAILED.
def dec(f):
    def w():
        return f()

    return w


@dec
def f(x):
    return x


f(1)
