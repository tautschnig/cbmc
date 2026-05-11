# Multi-level super() chain: C -> B -> A. PLR 9.2.2.
# Each class's __init__ calls super().__init__ to
# invoke the parent's initialization.


class A:
    def __init__(self, x: int) -> None:
        self.x = x


class B(A):
    def __init__(self, x: int, y: int) -> None:
        super().__init__(x)
        self.y = y


class C(B):
    def __init__(self, x: int, y: int, z: int) -> None:
        super().__init__(x, y)
        self.z = z


c = C(1, 2, 3)
# All three fields set through the chain.
assert c.x == 1
assert c.y == 2
assert c.z == 3


# Four-level chain: D -> C -> B -> A.
class D(C):
    def __init__(self, x: int, y: int, z: int, w: int) -> None:
        super().__init__(x, y, z)
        self.w = w


d = D(10, 20, 30, 40)
assert d.x == 10
assert d.y == 20
assert d.z == 30
assert d.w == 40
