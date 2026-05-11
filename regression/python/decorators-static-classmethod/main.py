# Decorators: @staticmethod and @classmethod (PLR 8.7).
# Also documents that @property has partial support —
# the method call emits but assignment of the result to
# a variable has a known tracking issue.


class C:
    count = 0

    @staticmethod
    def add(a: int, b: int) -> int:
        return a + b

    @classmethod
    def factory(cls) -> int:
        return 1


# @staticmethod: callable as C.f(a, b), no self injected.
r1 = C.add(3, 4)
assert r1 == 7

r2 = C.add(10, 20)
assert r2 == 30
