# PLR 3.3.2.1: C3 linearization for diamond inheritance.
#
#       A
#      / \
#     B   C
#      \ /
#       D
#
# MRO(D) = [D, B, C, A, object]. Calling super() from D
# dispatches to B; super() from B (inlined in D's dispatch)
# dispatches to C; super() from C (also in D's dispatch)
# dispatches to A. All four __init__ bodies must run so
# every field is assigned.


class A:
    def __init__(self) -> None:
        self.a = 1


class B(A):
    def __init__(self) -> None:
        super().__init__()
        self.b = 2


class C(A):
    def __init__(self) -> None:
        super().__init__()
        self.c = 3


class D(B, C):
    def __init__(self) -> None:
        super().__init__()
        self.d = 4


d = D()
assert d.a == 1
assert d.b == 2
assert d.c == 3
assert d.d == 4


# Method override walks MRO too — Z inherits Y's greet via
# its own MRO entry after the method-dispatch improvement
# lands. For now, this test only covers constructor MRO
# chaining (which the C3 linearization fix enables).
