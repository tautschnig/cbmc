class A:
    def f(self) -> int:
        return 1

class B:
    def __init__(self) -> None:
        self.a = A()

    def g(self) -> int:
        return self.a.f()

b = B()
assert b.g() == 1
