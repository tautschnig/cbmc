# Nested class method calls: a.b.method()
class A:
    def f(self) -> int:
        return 1

class B:
    def __init__(self) -> None:
        self.a = A()

b = B()
assert b.a.f() == 1
