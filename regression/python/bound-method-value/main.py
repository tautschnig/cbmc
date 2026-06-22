# PLR 3.3.2: a bound method `c.f` as a runtime value, flowing through a
# container, a conditional, and a function return, then called.
class C:
    def f(self) -> int:
        return 7

    def add(self, x: int) -> int:
        return x + 10


def pick(c: C):
    return c.f


c = C()
# container-stored
handlers = [c.f, c.add]
assert handlers[0]() == 7
assert handlers[1](5) == 15
# conditional
m = c.f if True else c.add
assert m() == 7
# returned from a function
r = pick(c)
assert r() == 7
