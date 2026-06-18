# PLR §8.7: missing required positional arg and "multiple values for
# argument" raise TypeError (uniform across free functions and methods).
def foo(w: int, x: int, y: int = 3) -> int:
    return x + y


# Valid calls must verify (no over-approximation).
assert foo(1, 2) == 5
assert foo(w=1, x=2) == 5
assert foo(1, x=2, y=7) == 9


class C:
    def m(self, a: int, b: int = 0) -> int:
        return a + b


c = C()
assert c.m(1) == 1
assert c.m(1, 2) == 3
