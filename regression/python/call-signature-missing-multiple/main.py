# PLR §8.7: missing required positional, "multiple values for argument",
# and missing required keyword-only arg all raise TypeError — uniform
# across free functions and methods. Valid calls must verify.
def foo(w: int, x: int, y: int = 3) -> int:
    return x + y


assert foo(1, 2) == 5
assert foo(w=1, x=2) == 5
assert foo(1, x=2, y=7) == 9


def kwo(*, a: int, b: int = 5) -> int:
    return a + b


assert kwo(a=1) == 6
assert kwo(a=1, b=2) == 3


class C:
    def m(self, a: int, b: int = 0) -> int:
        return a + b

    def k(self, *, p: int, q: int) -> int:
        return p + q


c = C()
assert c.m(1) == 1
assert c.m(1, 2) == 3
assert c.k(p=1, q=2) == 3
