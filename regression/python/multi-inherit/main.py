class A:
    def __init__(self) -> None:
        self.a: int = 1

class B:
    def __init__(self) -> None:
        self.b: int = 2

class C(A, B):
    def __init__(self) -> None:
        self.c: int = 3

c = C()
assert isinstance(c, C)
assert isinstance(c, A)
assert isinstance(c, B)
