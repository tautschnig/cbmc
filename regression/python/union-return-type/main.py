# Union[T1, T2] return-type inference. When a function has two
# return paths each producing a different class instance, the
# frontend widens the return type to python_value_type (tagged
# union) so the caller's 'is not None' checks work precisely.

class Foo:
    def __init__(self) -> None:
        self.tag = 1


class Bar:
    def __init__(self) -> None:
        self.tag = 2


def choose(flag: bool):
    if flag:
        return Foo()
    return Bar()


# Both paths return a concrete instance — never None.
x = choose(True)
assert x is not None

y = choose(False)
assert y is not None


# Mix with None — Optional[Union[Foo, Bar]].
def maybe_choose(which: int):
    if which == 1:
        return Foo()
    if which == 2:
        return Bar()
    return None


# When which==0, returns None; is-not-None holds only for
# which != 0. We assert the positive cases.
r1 = maybe_choose(1)
assert r1 is not None

r2 = maybe_choose(2)
assert r2 is not None

r3 = maybe_choose(0)
assert r3 is None
