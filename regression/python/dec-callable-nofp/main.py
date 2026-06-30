# PLR §8.7 no-false-positive guard for the decorator-callability check. None of
# these decorators is provably non-callable, so NONE must raise: an identity
# wrapper, a *args wrapper invoked with a mismatched count, and a callable class
# instance (`__call__`). Expected: VERIFICATION SUCCESSFUL (no spurious
# TypeError).


def identity(fn):
    return fn


def keep(fn):
    def w(*args, **kwargs):
        return fn(*args, **kwargs)

    return w


class Reg:
    def __call__(self, fn):
        return fn


r = Reg()


@identity
def a(x: int) -> int:
    return x


@keep
def b(x: int) -> int:
    return x


@r
def c(x: int) -> int:
    return x


assert a(1) == 1
assert b(2) == 2
assert c(3) == 3
