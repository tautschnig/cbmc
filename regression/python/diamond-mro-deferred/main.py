# PLR 3.3.2.1: diamond inheritance.
#
# This test documents the current state. Full C3 MRO is a
# known limitation: our super() handler picks
# class_bases[X][0] (first declared base) rather than
# walking the C3 linearization of the dispatch root's MRO.
#
# Single and linear (multi-level) inheritance work
# correctly (see regression/python/multi-level-super).
# True diamond (two bases sharing a common ancestor) does
# NOT chain through both sides — B's super() goes to A
# directly, skipping C.
#
# class_mro is computed at class-def time and stored per
# class; a future rewrite of the super() handler can walk
# it with a tracked root class to fix this.


# Linear 3-level inheritance: works correctly.
class A:
    def __init__(self) -> None:
        self.a = 1


class B(A):
    def __init__(self) -> None:
        super().__init__()
        self.b = 2


class C(B):
    def __init__(self) -> None:
        super().__init__()
        self.c = 3


c = C()
assert c.a == 1
assert c.b == 2
assert c.c == 3
