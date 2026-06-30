# KNOWNBUG: a decorator wrapper with a different arity than the call. `w()` takes
# no args but the decorated name is called with one -> CPython TypeError. The
# frontend does not track the wrapper's signature. Desired: VERIFICATION FAILED.
def dec(fn):
    def w():
        return fn()
    return w


@dec
def f(x: int) -> int:
    return x


f(1)
