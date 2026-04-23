class A:
    def get_value(self) -> int:
        return 42

class B:
    def __init__(self) -> None:
        self.a = A()

class C:
    def __init__(self) -> None:
        self.b = B()

c = C()
assert c.b.a.get_value() == 42
